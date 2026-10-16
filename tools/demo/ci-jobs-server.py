#!/usr/bin/env python3
"""Fake CI job executor: a host-side demo and test fixture for Pyxis HTTP work.

This is NOT a CI system. It runs nothing, keeps all state in memory, has no
authentication and no secrets, and exists only to give the Pyxis shell a small
JSON-over-HTTP service to talk to (screenshots, and HTTP task 5 qualification).
Do not expose it beyond a trusted LAN.

Endpoints (all JSON, pretty-printed, one request per connection):

  POST /jobs       Submit a job. Body: {"repo": "pyxis-os", "ref": "main",
                   "steps": ["build", "test", "image"]}. Replies 201 with the
                   job and a Location header, or 400 with {"error": ...}.
  GET  /jobs/<id>  The job's current state. It progresses with wall-clock time:
                   queued, running (current step and progress), then passed or
                   failed. Unknown ids reply 404.
  GET  /jobs       All jobs, newest first, with id, repo, ref and status.

Progress is deterministic. Step durations come from the step name plus a stable
hash of repo, ref and step. Every fourth job (id 4, 8, ...) fails: at its "test"
step if it has one, otherwise at its last step, with a log excerpt. The oldest
jobs are forgotten beyond 1000. --speed N makes everything N times faster.

Run it on the host:

  python3 tools/demo/ci-jobs-server.py                 # 0.0.0.0:18080
  python3 tools/demo/ci-jobs-server.py --port 8080 --speed 4

Try it with curl:

  curl -si -d '{"repo":"pyxis-os","ref":"main","steps":["build","test"]}' \\
      http://127.0.0.1:18080/jobs
  curl -s http://127.0.0.1:18080/jobs/1
  curl -s http://127.0.0.1:18080/jobs

From the Pyxis shell, with 192.168.0.213 standing for the host's address:

  cat json+http://192.168.0.213:18080/jobs/1
  cat json+http://192.168.0.213:18080/jobs

  api=$(http prepare POST json+http://192.168.0.213:18080/jobs)
  job=$(printf '{"repo":"pyxis-os","ref":"main","steps":["build","test"]}' > $api)
  cat $job
"""

import argparse
import json
import re
import sys
import threading
import time
import zlib
from datetime import datetime, timezone
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

MAX_BODY_BYTES = 16 * 1024
MAX_JOBS = 1000
MAX_STEPS = 10
QUEUE_DELAY_SECONDS = 2.0
FAIL_EVERY = 4
FAIL_FRACTION = 0.6

NAME_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9._/-]{0,99}")
STEP_PATTERN = re.compile(r"[a-z][a-z0-9_-]{0,31}")
REQUEST_FIELDS = {"repo", "ref", "steps"}
BASE_STEP_SECONDS = {"lint": 2.0, "build": 6.0, "test": 8.0, "image": 5.0, "deploy": 4.0}
DEFAULT_STEP_SECONDS = 3.0

TEST_FAILURE_LOG = [
  "running 148 tests",
  "test net::tcp_read_deadline ... ok",
  "test fs::rename_into_directory ... FAILED",
  "assertion failed: expected NOT_FOUND, got OK",
  "test result: FAILED. 147 passed; 1 failed",
]


class Job:
  """A submitted job and its fixed, precomputed timeline."""

  def __init__(self, job_id, repo, ref, steps, created, speed):
    self.id = job_id
    self.repo = repo
    self.ref = ref
    self.created = created
    self.started = created + QUEUE_DELAY_SECONDS / speed
    fails = job_id % FAIL_EVERY == 0
    failed_step = None
    if fails:
      failed_step = steps.index("test") if "test" in steps else len(steps) - 1
    self.failed_step = failed_step
    self.steps = []
    cursor = self.started
    self.nominal_total = 0.0
    for index, name in enumerate(steps):
      nominal = step_seconds(repo, ref, name) / speed
      self.nominal_total += nominal
      duration = nominal
      outcome = "passed"
      if failed_step is not None and index == failed_step:
        duration = nominal * FAIL_FRACTION
        outcome = "failed"
      if failed_step is not None and index > failed_step:
        self.steps.append({"name": name, "start": None, "end": None,
                           "duration": 0.0, "outcome": "skipped"})
        continue
      self.steps.append({"name": name, "start": cursor, "end": cursor + duration,
                         "duration": duration, "outcome": outcome})
      cursor += duration
    self.finished = cursor
    self.final_status = "failed" if failed_step is not None else "passed"

  def log_excerpt(self):
    if self.failed_step is None:
      return None
    name = self.steps[self.failed_step]["name"]
    if name == "test":
      return TEST_FAILURE_LOG
    return [f"{name}: step started", f"{name}: error: command exited with status 1"]

  def describe(self, now):
    status = "queued"
    if now >= self.finished:
      status = self.final_status
    elif now >= self.started:
      status = "running"

    steps = []
    current = None
    done_seconds = 0.0
    for step in self.steps:
      if step["outcome"] == "skipped":
        steps.append({"name": step["name"], "status": "skipped",
                      "started_at": None, "finished_at": None, "duration_seconds": None})
        continue
      if now >= step["end"]:
        state = step["outcome"]
        duration = step["duration"]
        finished_at = iso(step["end"])
        done_seconds += duration
      elif now >= step["start"]:
        state = "running"
        duration = now - step["start"]
        finished_at = None
        done_seconds += duration
        current = step["name"]
      else:
        steps.append({"name": step["name"], "status": "pending",
                      "started_at": None, "finished_at": None, "duration_seconds": None})
        continue
      steps.append({"name": step["name"], "status": state,
                    "started_at": iso(step["start"]), "finished_at": finished_at,
                    "duration_seconds": round(duration, 1)})

    if status == "passed":
      progress = 100
    else:
      progress = min(99, int(100 * done_seconds / self.nominal_total))
    finished = status in ("passed", "failed")
    job = {
      "id": self.id,
      "repo": self.repo,
      "ref": self.ref,
      "status": status,
      "current_step": current,
      "progress_percent": progress,
      "created_at": iso(self.created),
      "started_at": iso(self.started) if now >= self.started else None,
      "finished_at": iso(self.finished) if finished else None,
      "duration_seconds": None,
      "steps": steps,
    }
    if now >= self.started:
      job["duration_seconds"] = round(min(now, self.finished) - self.started, 1)
    if status == "failed":
      job["failed_step"] = self.steps[self.failed_step]["name"]
      job["log_excerpt"] = self.log_excerpt()
    return job


def step_seconds(repo, ref, name):
  jitter = (zlib.crc32(f"{repo}:{ref}:{name}".encode()) % 21) / 10.0
  return BASE_STEP_SECONDS.get(name, DEFAULT_STEP_SECONDS) + jitter


def iso(timestamp):
  return datetime.fromtimestamp(timestamp, timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


class Store:
  def __init__(self, speed):
    self.speed = speed
    self.lock = threading.Lock()
    self.jobs = {}
    self.next_id = 1

  def create(self, repo, ref, steps):
    with self.lock:
      job = Job(self.next_id, repo, ref, steps, time.time(), self.speed)
      self.jobs[job.id] = job
      self.next_id += 1
      while len(self.jobs) > MAX_JOBS:
        del self.jobs[min(self.jobs)]
      return job

  def get(self, job_id):
    with self.lock:
      return self.jobs.get(job_id)

  def newest_first(self):
    with self.lock:
      return [self.jobs[job_id] for job_id in sorted(self.jobs, reverse=True)]


class RequestError(Exception):
  def __init__(self, status, message):
    super().__init__(message)
    self.status = status
    self.message = message


def validate_request(body):
  try:
    request = json.loads(body.decode("utf-8"))
  except (UnicodeDecodeError, ValueError):
    raise RequestError(HTTPStatus.BAD_REQUEST, "body is not valid JSON")
  if not isinstance(request, dict):
    raise RequestError(HTTPStatus.BAD_REQUEST, "body must be a JSON object")
  unknown = sorted(set(request) - REQUEST_FIELDS)
  if unknown:
    raise RequestError(HTTPStatus.BAD_REQUEST, f"unknown field: {unknown[0]}")
  for field in ("repo", "ref"):
    value = request.get(field)
    if not isinstance(value, str) or not NAME_PATTERN.fullmatch(value):
      raise RequestError(HTTPStatus.BAD_REQUEST,
                         f"{field} must be a string of letters, digits and . _ / - "
                         "(1 to 100 characters)")
  steps = request.get("steps")
  if not isinstance(steps, list) or not 1 <= len(steps) <= MAX_STEPS:
    raise RequestError(HTTPStatus.BAD_REQUEST,
                       f"steps must be a list of 1 to {MAX_STEPS} step names")
  for step in steps:
    if not isinstance(step, str) or not STEP_PATTERN.fullmatch(step):
      raise RequestError(HTTPStatus.BAD_REQUEST,
                         "each step must be a lowercase name such as \"build\"")
  if len(set(steps)) != len(steps):
    raise RequestError(HTTPStatus.BAD_REQUEST, "step names must be unique")
  return request["repo"], request["ref"], steps


class Handler(BaseHTTPRequestHandler):
  protocol_version = "HTTP/1.1"
  server_version = "pyxis-ci-jobs-demo"
  store = None
  quiet = False

  def log_message(self, format, *args):
    if not self.quiet:
      super().log_message(format, *args)

  def send_json(self, status, document, headers=()):
    body = (json.dumps(document, indent=2) + "\n").encode("utf-8")
    self.send_response(status)
    self.send_header("Content-Type", "application/json")
    self.send_header("Content-Length", str(len(body)))
    self.send_header("Connection", "close")
    for name, value in headers:
      self.send_header(name, value)
    self.end_headers()
    if self.command != "HEAD":
      self.wfile.write(body)
    self.close_connection = True

  def send_error(self, code, message=None, explain=None):
    status = HTTPStatus(code)
    self.send_json(status, {"error": message or status.phrase})

  def route(self):
    path = self.path.split("?", 1)[0].split("#", 1)[0]
    if path in ("/jobs", "/jobs/"):
      return "collection", None
    match = re.fullmatch(r"/jobs/([0-9]+)", path)
    if match:
      return "job", int(match.group(1))
    return None, None

  def read_body(self):
    if "chunked" in self.headers.get("Transfer-Encoding", "").lower():
      body = b""
      while True:
        size_line = self.rfile.readline(64).split(b";", 1)[0].strip()
        try:
          size = int(size_line, 16)
        except ValueError:
          raise RequestError(HTTPStatus.BAD_REQUEST, "malformed chunked body")
        if size == 0:
          while self.rfile.readline(1024) not in (b"\r\n", b"\n", b""):
            pass
          return body
        if len(body) + size > MAX_BODY_BYTES:
          raise RequestError(HTTPStatus.REQUEST_ENTITY_TOO_LARGE, "body too large")
        body += self.rfile.read(size)
        self.rfile.readline(8)
    length = self.headers.get("Content-Length")
    if length is None or not length.isdigit():
      raise RequestError(HTTPStatus.LENGTH_REQUIRED, "Content-Length is required")
    if int(length) > MAX_BODY_BYTES:
      raise RequestError(HTTPStatus.REQUEST_ENTITY_TOO_LARGE, "body too large")
    return self.rfile.read(int(length))

  def do_GET(self):
    kind, job_id = self.route()
    now = time.time()
    if kind == "collection":
      jobs = [{"id": job.id, "repo": job.repo, "ref": job.ref,
               "status": job.describe(now)["status"]}
              for job in self.store.newest_first()]
      self.send_json(HTTPStatus.OK, {"jobs": jobs})
    elif kind == "job":
      job = self.store.get(job_id)
      if job is None:
        self.send_json(HTTPStatus.NOT_FOUND, {"error": f"no job {job_id}"})
      else:
        self.send_json(HTTPStatus.OK, job.describe(now))
    else:
      self.send_json(HTTPStatus.NOT_FOUND, {"error": "not found"})

  do_HEAD = do_GET

  def do_POST(self):
    kind, _ = self.route()
    if kind != "collection":
      status = HTTPStatus.METHOD_NOT_ALLOWED if kind == "job" else HTTPStatus.NOT_FOUND
      headers = [("Allow", "GET")] if kind == "job" else []
      self.send_json(status, {"error": status.phrase.lower()}, headers)
      return
    try:
      repo, ref, steps = validate_request(self.read_body())
    except RequestError as error:
      self.send_json(error.status, {"error": error.message})
      return
    job = self.store.create(repo, ref, steps)
    self.send_json(HTTPStatus.CREATED, job.describe(time.time()),
                   [("Location", f"/jobs/{job.id}")])

  def do_PUT(self):
    self.send_json(HTTPStatus.METHOD_NOT_ALLOWED, {"error": "method not allowed"})

  do_DELETE = do_PATCH = do_PUT


def main():
  parser = argparse.ArgumentParser(
      description="Fake CI job executor, a demo and test fixture for Pyxis HTTP work.")
  parser.add_argument("--host", default="0.0.0.0", help="address to bind (default 0.0.0.0)")
  parser.add_argument("--port", type=int, default=18080, help="port to listen on (default 18080)")
  parser.add_argument("--speed", type=float, default=1.0,
                      help="run jobs this many times faster (default 1)")
  parser.add_argument("--quiet", action="store_true", help="do not log requests")
  arguments = parser.parse_args()
  if arguments.speed <= 0:
    parser.error("--speed must be positive")

  Handler.store = Store(arguments.speed)
  Handler.quiet = arguments.quiet
  server = ThreadingHTTPServer((arguments.host, arguments.port), Handler)
  server.daemon_threads = True
  print(f"fake CI job executor (demo fixture, in-memory) on "
        f"http://{arguments.host}:{arguments.port}/jobs", file=sys.stderr)
  try:
    server.serve_forever()
  except KeyboardInterrupt:
    pass
  finally:
    server.server_close()


if __name__ == "__main__":
  main()

# Shell basics

The shell reads one command per line and runs it. Its prompt shows the current
directory, so `home://notes>` means you are in the `notes` directory of
`home://`. The examples below start in an empty home.

## Run commands

1. `echo` prints its arguments:

   ```text
   home://> echo hello
   hello
   ```

2. `>` writes a command's output to a file. `cat` prints a file:

   ```text
   home://> echo hello > note.txt
   home://> cat note.txt
   hello
   ```

3. Quote an argument that contains spaces or special characters, with single
   or double quotes:

   ```text
   home://> mkdir 'two words'
   ```

## Directories and files

1. `mkdir` makes a directory, `cd` enters it and `ls` lists it. `cd ..` goes up:

   ```text
   home://> mkdir notes
   home://> cd notes
   home://notes> echo one > a.txt
   home://notes> echo two > b.txt
   home://notes> ls
   a.txt  b.txt
   home://notes> cd ..
   home://>
   ```

2. `cp` copies, `mv` moves or renames and `rm` removes:

   ```text
   home://notes> cp a.txt c.txt
   home://notes> mv c.txt d.txt
   home://notes> rm d.txt
   home://notes> ls
   a.txt  b.txt
   ```

   `rm -r` removes a directory and everything in it.

## Wildcards

An unquoted `*` stands for any run of characters in a file name. The shell
replaces the word with the matching names, in sorted order, before it runs the
command:

```text
home://notes> echo *.txt
a.txt b.txt
home://notes> cat a.txt b.txt
one
two
```

- `*` works only in the last part of a path: `notes/*.txt` is fine, and
  `*/a.txt` is refused:

  ```text
  shell: Wildcards are supported only in the last path component: */a.txt
  ```

- A name that starts with `.` matches only a pattern that starts with `.`, so
  `*` does not match `.history`.
- If nothing matches, the command does not run:

  ```text
  home://notes> echo *.md
  shell: No match: *.md (quote it to pass it literally)
  ```

- Quote the `*` to pass it through unchanged:

  ```text
  home://notes> echo '*.txt'
  *.txt
  ```

## Add to a file

`>` replaces a file. `>>` adds to the end of it, and creates the file if it
does not exist:

```text
home://notes> echo first >> log.txt
home://notes> echo second >> log.txt
home://notes> cat log.txt
first
second
```

`>>` never removes what is already there. When two shells add to the same file
at the same time, neither overwrites the other's text.

## Edit the line

- Press Tab to complete a command or file name; see
  [Complete names](#complete-names).
- Press Ctrl+C to throw away the line you are typing.
- Press Ctrl+C while a command runs to stop it:

  ```text
  home://> sleep 30
  shell: sleep: Process terminated
  ```

## Complete names

Tab completes the word before the cursor. Add two more files to `notes`:

```text
home://notes> echo draft > report.txt
home://notes> cp bin://echo.pxe say.pxe
```

1. Type the start of a file name and press Tab. The shell finishes the name
   and adds a space:

   ```text
   home://notes> cat rep<Tab>
   home://notes> cat report.txt
   ```

2. The first word of a line completes to a command:

   ```text
   home://notes> ec<Tab>
   home://notes> echo
   ```

3. A program in the current directory needs `./`, and completes after it:

   ```text
   home://notes> ./sa<Tab>
   home://notes> ./say.pxe hello
   hello
   ```

4. Paths into other directories and roots complete the same way. A directory
   ends with `/`, so the next Tab goes one level deeper:

   ```text
   home://notes> ls ../notes/sa<Tab>
   home://notes> ls ../notes/say.pxe
   home://notes> ls system://co<Tab>
   home://notes> ls system://config/
   home://notes> bin://ech<Tab>
   home://notes> bin://echo.pxe
   ```

   Starting a root name offers the root: `ls hom` completes to `ls home://`.

5. When several names fit, the first Tab adds what they share. If that adds
   nothing, press Tab again to list them under the line:

   ```text
   home://notes> ./<Tab><Tab>
   ./a.txt       ./b.txt       ./log.txt     ./report.txt  ./say.pxe
   ```

   A short start can match files and roots together: `cat a` lists `a.txt`
   and any root that starts with `a`, such as `acpi://`. Type another letter
   to narrow it.

Tab never starts a network request, so `http://` and `https://` words are not
completed.

## History

The shell remembers what you typed, and keeps it after a reboot.

1. Press Up to bring back an earlier line, Down to go forward. Edit the line
   if you like, then press Enter to run it.
2. Press Ctrl+R and type part of a line to search backwards:

   ```text
   (reverse-i-search)'mv': mv c.txt d.txt
   ```

   Press Ctrl+R again for an older match, Enter to run the line, or Esc to
   keep it for editing. Ctrl+G cancels the search.
3. Start a line with a space to keep it out of the saved history.

## Make shortcuts

An alias is a short name for a command with fixed arguments. Aliases live in
`home://.config/shell/startup.lua`. The shell reads that file once, when it
starts.

1. Create the file. This one makes `ll` mean `ls -l`:

   ```text
   home://> mkdir .config
   home://> mkdir .config/shell
   home://> echo 'return { aliases = { ll = { "ls", "-l" } } }' > .config/shell/startup.lua
   ```

   Each alias is a name and a list of strings: the command, then its
   arguments. To change the file later, open it with `vi`
   (`vi .config/shell/startup.lua`).

2. Start a new shell. On an installed system, run `reboot`. Then use the
   alias, and ask `which` what it stands for:

   ```text
   home://> ll notes
   file           4 a.txt
   file           4 b.txt
   file          13 log.txt
   ...
   home://> which ll
   ll: alias 'ls' '-l'
   ```

   Whatever you type after the alias is added to its arguments. Tab offers
   alias names with the commands.

3. To run the real command instead of the alias, quote the name (`'ls'`) or
   write `\ls`.

The same file can also set variables for the shell (`variables`) and for the
programs it starts (`exported`). Values in it are plain text: the shell never
runs the file as code. If the file has a mistake, the shell says what is wrong
when it starts and ignores the whole file:

```text
shell: home://.config/shell/startup.lua: an alias must be a nonempty string array; none applied
```

## Clear and reset

- `clear` empties the screen:

  ```text
  home://notes> clear
  ```

- `reset` puts the terminal back to normal if a program left it in a strange
  state, such as a hidden cursor or leftover colours. It does not erase the
  screen:

  ```text
  home://notes> reset
  ```

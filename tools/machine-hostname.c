#include <machine_settings.h>

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
  if (argc != 3) {
    fprintf(stderr, "Usage: machine-hostname LIVE_CONFIG HOSTNAME\n");
    return 1;
  }
  if (!machine_hostname_valid(argv[2], strlen(argv[2]))) {
    fprintf(stderr, "HOSTNAME must be one ASCII label of 1..63 letters, digits or internal hyphens\n");
    return 1;
  }

  FILE *source = fopen(argv[1], "rb");
  if (!source) {
    perror(argv[1]);
    return 1;
  }

  /* Keep the selected archive configuration intact, then replace one field. */
  fputs("local config = (function()\n", stdout);
  char buffer[4096];
  size_t length;
  while ((length = fread(buffer, 1, sizeof(buffer), source))) {
    if (fwrite(buffer, 1, length, stdout) != length) {
      perror("live configuration output");
      fclose(source);
      return 1;
    }
  }
  if (ferror(source)) {
    perror(argv[1]);
    fclose(source);
    return 1;
  }
  if (fclose(source)) {
    perror(argv[1]);
    return 1;
  }

  /* The shared validator's alphabet needs no Lua string escaping. */
  printf("\nend)()\nconfig.hostname = \"%s\"\nreturn config\n", argv[2]);
  if (fflush(stdout) || ferror(stdout)) {
    perror("live configuration output");
    return 1;
  }
  return 0;
}

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

## Edit the line

- Press Tab to complete a name. With `cat a` typed, Tab gives `cat a.txt `.
- Press Ctrl+C to throw away the line you are typing.
- Press Ctrl+C while a command runs to stop it:

  ```text
  home://> sleep 30
  shell: sleep: Process terminated
  ```

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

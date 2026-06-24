# IM-WHS

the project contains 2 directories, each contain their own readme with the
instruction to run

- [Mobile App](./app/)
- [Microcontroller code](./imwhs_microctl/)

## powershell tutorial

you will need to use the powershell to run scripts so you need to understand
what is powershell (terminal in general)

just like the file-explorer that you use to navigate your directories (aka folders)
a terminal has a working directory, to know your current working dir you can use
`pwd` (print working directory) as follows

```powershell
pwd .

# you will get something like
# C:\Users\Default
```

`pwd` in this case is called a "command" and the `.` is an argument

a command is a order for the computer to execute,
an argument is a piece of information that the command needs to know to be able
to execute (optional, `pwd` defaults to `.` for example)

for example the command `rm` (remove) needs to know what files or directories
to remove

```powershell
rm main.cpp
```

in the example above `rm` is the command and `main.cpp` is the argument

the dot you seen earlier is a special argument that means here (current working
dir), while `..` means the parent directory

`cd` (change directory) is another useful command that lets you navigate your
directories

here is an example of how to use `cd` with `pwd` to navigate your directories

```bash
❯ pwd
/home/user/dummy/example

❯ cd ..

❯ pwd
/home/user/dummy

❯ cd example

❯ pwd
/home/user/dummy/example
```

**working with powershell sometimes you need to change the execution policy** to
allow scripts to run

here is how to do that

```powershell
# to get current
Get-ExecutionPolicy

# to change it the policy itself and the scope
# the policy is the permission level
# notice the special argument with `-` this is called flag it is not argument
# by itself but it specify the type of the argument after it for the command parser
Set-ExecutionPolicy Bypass -Scope Process

# other polices (Restricted, AllSigned, RemoteSigned, Unrestricted, Bypass, Undefined)
# other scopes (Process, CurrentUser, LocalMachine)
```

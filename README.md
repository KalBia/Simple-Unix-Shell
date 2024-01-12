## Simple Unix Shell
Technology: C\
Type of project: fill-in-the-gaps

### Rules of project
Only the code inside the "STUDENT" zones is written from scratch by me. The professor provided everything else.\
The shell only handles inputs that fulfil grammar:\
\
%start program\
%%\
program : pipe_sequence ’&’ | pipe_sequence;\
pipe_sequence : command | pipe_sequence ’|’ command;\
command : cmd_words cmd_suffix | cmd_words;\
cmd_words : cmd_words WORD | WORD;\
cmd_suffix : io_redirect | cmd_suffix io_redirect;\
io_redirect : ’<’ WORD | ’>’ WORD;\

### How to use it
make - compile the project\
make format - format all .c files\
make test - run tests given by the professor (it requires that the code outside the "STUDENT" zones is unmodified, even adding comments is forbidden)

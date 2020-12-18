/*
A parser which reads preprocessed C code (with linemarks) and generates a
file for each enum which is prefixed by an annotation keyword. See below for
command line options. Such parsing may be used to generate data structures
based on the enum. By default this program generates a NULL terminated array
of names for each enumeration.

gcc -E cenum.c | ./cenum

Copyright 2020 James R.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

struct substitution {
	int type;/* first letter of subtitution pseudovariable */
	int index;/* beginning of subtitution in pattern */
	const char * suffix;/* text after substitution */
	struct substitution *next;
};

struct printer {
	const char * text;
	struct substitution * variables;
};

/*
substitutions:

$n, $(name) - identifier of enum
$k, $(key)  - enumeration
*/

/* -o */const char * target_directory = "-";
/* -x */const char * extension = ".c";

/* -h */const char * header = "const char*const $n[]={";
/* -t */const char * footer = "NULL};\n";

/* -e */const char * pattern = "\"$k\",";

/* -s */const char * annotation = "";

#define PUTSF(fmt, ...) fmt "\n", __VA_ARGS__
#define putsf(...) fprintf(stderr, PUTSF(__VA_ARGS__,0))
#define exitwerror(...) putsf(__VA_ARGS__), exit(1)

int set_option
(		int     ac,
		char ** av,
		int     n,
		const char * const options,
		const char ** const values[])
{
	char *a = av[n];
	char *p;

	const char **v;

	if (a[0] == '-' && (p = strchr(options, a[1])))
	{
		v = values[p - options];

		if (a[2] != '\0')
			return (*v = &a[2]), 1;
		else
		{
			if (++n < ac)
				return (*v = av[n]), 2;
			else
				exitwerror("'%s': missing argument", a);
		}
	}
	else
		exitwerror("'%s': unknown option", a);
}

void check_options
(		int     ac,
		char ** av)
{
	const char ** const options[] = {
		&target_directory,
		&extension,
		&header,
		&footer,
		&pattern,
		&annotation,
	};

	int n = 1;

	if (strcmp(av[1], "-v") == 0)
	{
		puts("cenum 1");
		exit(0);
	}

	while (n < ac)
		n += set_option(ac, av, n, "oxhtes", options);
}

int isword (int c)
{
	return isalnum(c) || c == '_';
}

char * skip_word (char *p)
{
	int c = *p;
	if (isword(c))
		while (++p, isword(*p)) ;
	else
		while (++p, *p == c) ;
	return p;
}

static char *eot;

void reset_parser (void)
{
	eot = NULL;
}

char * c_token (char *p)
{
	static int oldc;

	if (eot)
	{
		*eot = oldc;

		if (oldc == '"' && *p == '"')
			eot++;

		p = eot;
	}

	p += strspn(p, "\n ");

	if (*p == '\0')
		return reset_parser(), NULL;
	else if (*p == '"')
	{
		eot = p;
		do
		{
			eot = strchr(&eot[1], '"');

			if (eot == NULL)
				return NULL;
		}
		while (eot[-1] == '\\');
	}
	else
		eot = skip_word(p);

	oldc = *eot;
	*eot = '\0';
	return p;
}

char * required_token (char *p)
{
	if ((p = c_token(p)) == NULL)
		exitwerror("missing linemark");
	else
		return p;
}

int adjust_nest (char *p)
{
	switch (*p)
	{
		case '(':
			return strlen(p);
		case ')':
			return -(strlen(p));
		default:
			return 0;
	}
}

FILE * open_out (char *p)
{
	FILE *o;
	char filename[1024];

	if (strcmp(target_directory, "-"))
	{
		snprintf(filename, sizeof filename,
				"%s/%s%s", target_directory, p, extension);
		o = fopen(filename, "w");

		if (o == NULL)
		{
			perror(filename);
			exit(1);
		}
		else
			return o;
	}
	else
		return stdout;
}

void compile (struct printer *printer, const char *pattern)
{
	struct substitution *o = NULL;
	struct substitution **next = &o;
	const char *p = pattern;
	int n = 0;
	while ((n = strcspn(p, "$")), p = &p[n], *p == '$')
	{
		*next = malloc(sizeof o);

		if (p[1] == '(')
		{
			(*next)->suffix = strchr(p, ')');

			if ((*next)->suffix == NULL)
				exitwerror("%s: malformed pattern", pattern);

			(*next)->suffix++;
			(*next)->type = p[2];
		}
		else
		{
			(*next)->suffix = &p[2];
			(*next)->type = p[1];
		}

		(*next)->index = n;

		p = (*next)->suffix;
		next = &(*next)->next;
	}
	printer->text = pattern;
	printer->variables = o;
}

void substitute
(		FILE *file,
		struct printer *o,
		const char * const options,
		char * const values[])
{
	const char *t = o->text;
	struct substitution *v = o->variables;
	char *p;
	while (v)
	{
		fwrite(t, 1, v->index, file);

		if ((p = strchr(options, v->type)))
		{
			fputs(values[p - options], file);
		}

		t = v->suffix;
		v = v->next;
	}
	fputs(t, file);
}

int main
(		int     ac,
		char ** av)
{
	enum mode {
		NOTE,
		ID,
		KEY,
		VALUE,
	};

	int default_mode;
	const char * default_next;

	int mode;
	const char * next;
	const char * alt = NULL;

	char line[241];
	char *p;

	int line_count = 0;
	char *filename = NULL;

#define parser_warning(w) putsf("%s:%d: " w, filename, line_count)
#define malformed_input() parser_warning("malformed input"), exit(1)

	int nest = 0;

	FILE *file;

	struct printer header_printer;
	struct printer footer_printer;
	struct printer pattern_printer;

	char *values[2] = {0};

	check_options(ac, av);

	compile(&header_printer, header);
	compile(&footer_printer, footer);
	compile(&pattern_printer, pattern);

	if (strcmp(annotation, ""))
	{
		default_mode = NOTE;
		default_next = annotation;
	}
	else
	{
		default_mode = ID;
		default_next = "enum";
	}

	mode = default_mode;
	next = default_next;

	while (fgets(line, sizeof line, stdin))
	{
		line_count++;

		if (line[0] == '#')
		{
			p = &line[1];
			free(filename);
			line_count = atoi((p = required_token(p)));
			filename = strdup(&(p = required_token(p))[1]);
			reset_parser();
		}
		else
		{
			p = line;
			while ((p = c_token(p)))
			{
				if (mode == KEY && *p == '}')
				{
					substitute(file, &footer_printer, "n", values);
					if (file != stdout)
						fclose(file);
					mode = default_mode;
					next = default_next;
				}
				else if (next)
				{
					if (strcmp(p, next))
					{
						if (mode != NOTE && (mode != ID || strcmp(annotation, "")))
						{
							if (alt && strcmp(p, alt) == 0)
							{
								if (mode == KEY)
									next = NULL;
								else if (mode == VALUE)
								{
									if (nest)
										malformed_input();
									else
									{
										substitute(file, &footer_printer, "n", values);
										fclose(file);
										mode = default_mode;
										next = default_next;
									}
								}
							}
							else if (mode == VALUE)
								nest += adjust_nest(p);
							else
								malformed_input();
						}
					}
					else
					{
						switch (mode)
						{
							case NOTE:
								mode = ID;
								next = "enum";
								break;
							case ID:
								if (*p == '{')
									mode = KEY;
								next = NULL;
								break;
							case KEY:
								mode = VALUE;
								next = NULL;
								break;
							case VALUE:
								if (!nest)
								{
									mode = KEY;
									next = NULL;
								}
								break;
						}
					}
				}
				else if (mode == ID && *p == '{')
				{
					parser_warning("cannot parse anonymous enum");
					mode = default_mode;
					next = default_next;
				}
				else if (mode == VALUE)
				{
					nest = adjust_nest(p);
					next = ",";
					alt = "}";
				}
				else if (isword(*p))
				{
					if (mode == ID)
					{
						file = open_out(p);
						if (values[0])
							free(values[0]);
						values[0] = strdup(p);
						substitute(file, &header_printer, "n", values);
						next = "{";
					}
					else if (mode == KEY)
					{
						values[1] = p;
						substitute(file, &pattern_printer, "nk", values);
						next = "=";
						alt = ",";
					}
				}
				else
					malformed_input();
			}
		}
	}

	exit(0);
}

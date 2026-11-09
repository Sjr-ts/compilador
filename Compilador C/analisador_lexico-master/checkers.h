#ifndef CHECKERS
#define CHECKERS

int check_delimiter(const char character);

int check_assignment(const char character);

int check_arithmetic_operator(const char character);

int check_relational_operator(const char *word);

int check_reserved_word(const char *word);

int check_integer(const char *word);

int check_float(const char *word);

int check_character(const char *word);

int check_string(const char *word);

#endif

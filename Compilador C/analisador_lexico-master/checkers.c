#include "checkers.h"
#include <ctype.h>
#include <string.h>

static const char *const RESERVED_WORDS[] = {"int",  "float", "char", "if",
                                             "else", "while", "for",  "return"};

static const char DELIMITERS[] = {';', '(', ')', '{', '}'};

static const char ARITHMETIC_OPERATORS[] = {'+', '-', '*', '/'};

static const char *const RELATIONAL_OPERATORS[] = {"==", ">",  "<",
                                                   ">=", "<=", "!="};

#define RESERVED_WORDS_SIZE (sizeof RESERVED_WORDS / sizeof RESERVED_WORDS[0])
#define DELIMITERS_SIZE (sizeof DELIMITERS / sizeof DELIMITERS[0])
#define ARITHMETIC_OPERATORS_SIZE                                              \
  (sizeof ARITHMETIC_OPERATORS / sizeof ARITHMETIC_OPERATORS[0])
#define RELATIONAL_OPERATORS_SIZE                                              \
  (sizeof RELATIONAL_OPERATORS / sizeof RELATIONAL_OPERATORS[0])

int check_delimiter(const char character) {
  int i;

  for (i = 0; i < (int)DELIMITERS_SIZE; i++) {
    if (DELIMITERS[i] == character) {
      return 1;
    }
  }

  return 0;
}

int check_assignment(const char character) {
  if (character == '=') {
    return 1;
  }

  return 0;
}

int check_arithmetic_operator(const char character) {
  int i;

  for (i = 0; i < (int)ARITHMETIC_OPERATORS_SIZE; i++) {
    if (ARITHMETIC_OPERATORS[i] == character) {
      return 1;
    }
  }

  return 0;
}

int check_relational_operator(const char *word) {
  int i;

  for (i = 0; i < (int)RELATIONAL_OPERATORS_SIZE; i++) {
    if (strcmp(RELATIONAL_OPERATORS[i], word) == 0) {
      return 1;
    }
  }

  return 0;
}

int check_reserved_word(const char *word) {
  int i;

  for (i = 0; i < (int)RESERVED_WORDS_SIZE; i++) {
    if (strcmp(RESERVED_WORDS[i], word) == 0) {
      return 1;
    }
  }

  return 0;
}

int check_integer(const char *word) {
  int i, size;
  size = strlen(word);

  for (i = 0; i < size; i++) {
    if (isdigit((char)word[i])) {
      continue;
    } else {
      return 0;
    }
  }

  return 1;
}

int check_float(const char *word) {
  int i, size, has_dot;
  char c;
  size = strlen(word);

  has_dot = 0;
  for (i = 0; i < size; i++) {
    c = word[i];
    if (isdigit(c)) {
      continue;
    } else if (c == '.') {
      has_dot = 1;
      continue;
    } else {
      return 0;
    }
  }

  if (!has_dot) {
    return 0;
  }

  return 1;
}

int check_character(const char *word) {
  int size;

  size = strlen(word);
  if (size != 3) {
    return 0;
  }

  if (!(word[0] == '\'' && word[size - 1] == '\'')) {
    return 0;
  }

  return 1;
}

int check_string(const char *word) {
  int size;

  size = strlen(word);
  if (word[0] == '\"' && word[size - 1] == '\"') {
    return 1;
  }

  return 0;
}

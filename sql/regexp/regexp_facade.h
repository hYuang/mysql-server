#ifndef SQL_REGEXP_REGEXP_FACADE_H_
#define SQL_REGEXP_REGEXP_FACADE_H_

/* Copyright (c) 2017, 2018 Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have included with MySQL.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

/**
  @file regexp_facade.h

  This file hides most of ICU from the Item_func_regexp subclasses.
*/

#include <stdint.h>

#include "nullable.h"
#include "sql/item.h"
#include "sql/regexp/regexp_engine.h"
#include "sql_string.h"

extern int32_t opt_regexp_time_limit;
extern int32_t opt_regexp_stack_limit;

namespace regexp {

/**
   Evaluates an expression to a string value, performing character set
   conversion to regexp_lib_charset if necessary. There are three cases:

   -# If the expression expr is a string constant already in the needed
   character set, a shallow pointer to its character data is returned.

   -# If the expression expr is a string constant not in the needed
   character set, the same process as in #4 is employed.

   -# If the expression is not a string, but the server's character set
   matches that if the library. the function attempts to render it as a
   string directly in the out buffer.

   -# If the expression is not a string, and the server's character set
   differs from that if the library. the function attempts to render it as
   a string to an intermediate buffer. The buffer is then copied as part of
   conversion into the output buffer.

   All of the above follows how the server generally handles character set
   conversion, but is usually not documented.

   @param expr The expression to be printed.

   @param[out] out The String object whose internal buffer will contain the
   result in case of copying.
*/
String *EvalExprToCharset(Item *expr, String *out);

/**
  This class handles

  - Conversion to the regexp library's character set, and buffers the
    converted strings during matching.

  - Re-compilation of the regular expression in case the pattern is a field
    reference or otherwise non-constant.

  - `NULL` handling.

  - Conversion between indexing conventions. Clients of this class can use
    one-based indexing, while the classes used by this class use zero-based
    indexing.
*/
class Regexp_facade {
 public:
  explicit Regexp_facade(uint flags)
      : m_flags(flags),
        m_current_subject(static_cast<const char *>(nullptr), 0,
                          regexp_lib_charset) {}

  /**
    Sets the pattern if called for the first time or the pattern_expr is
    non-constant. This function is meant to be called for every row in a
    command such as

      SELECT regexp_like( column, 'a+' ) FROM table;

    In this case, the client of this class may call SetPattern() for every
    row without paying any penalty, as this becomes a no-op for all
    consecutive calls. In cases such as

      SELECT regexp_like( column, regexp_column ) FROM table;

    The `regexp_column` expression is non-constant and hence we have to
    recompile the regular expression for each row.
  */
  bool SetPattern(Item *pattern_expr);

  /**
    Tries to match the subject against the compiled regular expression.

    @param subject_expr Is evaluated into a string to search.
    @param start Start position, 1-based.
    @param occurrence Which occurrence of the pattern should be searched for.

    @retval true A match was found.
    @retval false A match was not found.

    @retval nullptr Either the engine was not compiled, or subject_expr
    evaluates to NULL. This is useful for the Item_func_regexp object, since it
    doesn't have to make a special case for when the regular expression is
    NULL. Instead, the case is handled here in the facade.
  */
  Mysql::Nullable<bool> Matches(Item *subject_expr, int start, int occurrence);

  /**
    Searches the subject for a match of the compiled regular expression and
    returns a position.

    @param subject_expr The string to search.
    @param start Start position, 1-based.
    @param occurrence Which occurrence of the pattern should be searched for.
    @param after_match If true, the position following the end of the match
    is returned. If false, the position before the match is returned.

    @return The first character of the match, or a null value if not found.
  */
  Mysql::Nullable<int> Find(Item *subject_expr, int start, int occurrence,
                            bool after_match);

  /**
    @param subject_expr The string to search.
    @param replacement_expr The string to replace the match with.
    @param start Start position, 1-based.
    @param occurrence Which occurrence of the pattern should be searched for.
    @param[in,out] result Holds the buffer for writing the result.
  */
  String *Replace(Item *subject_expr, Item *replacement_expr, int64_t start,
                  int occurrence, String *result);

  String *Substr(Item *subject_expr, int start, int occurrence, String *result);

  /**
    Returns the substring that was matched by the previous call to find() or
    matches().

    @param[out] result A string we can write to.
    @return A pointer to result.
  */
  String *MatchedSubstring(String *result) {
    return m_engine->MatchedSubstring(result);
  }

 private:
  /**
    Resets the compiled regular expression with a new string.

    @param subject_expr The new string to search.

    @retval false OK.
    @retval true Either there is no compiled regular expression, or the
    expression evaluated to `NULL`.
  */
  bool Reset(Item *subject_expr);

  /**
    Actually compiles the regular expression.
  */
  bool SetupEngine(Item *pattern_expr, uint flags);

  uint m_flags;

  /**
    Used for all the actual regular expression matching, search-and-replace,
    and positional and string information. If either the regular expression
    pattern or the subject is `NULL`, this pointer is empty.
  */
  unique_ptr_destroy_only<Regexp_engine> m_engine;

  /**
    ICU does not copy the subject string, so we keep the buffer here. It is
    overwritten in reset(). It is treated as a UChar buffer inside ICU, and is
    expected to be aligned as such. So we construct this class with
    regexp_lib_charset.

    @see Regexp_engine::reset()
  */
  String m_current_subject;

  /**
    `NULL` handling. In case the reset() function is called with a `NULL`
    value, or some expression that evaluates to it, all matching functions
    will return `NULL`. This avoids having to handle the case in all the
    val_xxx() function in the Item_func_regexp subclasses.
  */
  bool m_is_reset_with_null_string;

  Item *m_pattern{nullptr};
};

}  // namespace regexp

#endif  // SQL_REGEXP_REGEXP_FACADE_H_

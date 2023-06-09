#ifndef STRUTILS_H
#define STRUTILS_H

#include <unicode/utf8.h>
#include <unicode/uchar.h>
#include <unicode/brkiter.h>
#include <unicode/unistr.h>
#include <unicode/ucnv.h>

#include <algorithm>
#include <functional>
#include <cctype>
#include <locale>
#include <string>

#include "base/internal/EGCIterator.h"

// --------------------------------------------------------------------------
/// @brief removes given characters from beginning of string
///
/// @param str   string to be trimmed
/// @param chars characters to be removed, defaults to whitespace
///
/// @return returns trimmed string
// --------------------------------------------------------------------------
inline std::string &ltrim(std::string &&str, const std::string &chars = "\t\n\v\f\r ") {
  str.erase(0, str.find_first_not_of(chars));
  return str;
}

inline std::string ltrim(const std::string &str, const std::string &chars = "\t\n\v\f\r ") {
  std::string ret{str};

  ret.erase(0, str.find_first_not_of(chars));
  return ret;
}

// --------------------------------------------------------------------------
/// @brief removes given characters from end of string
///
/// @param str   string to be trimmed
/// @param chars characters to be removed, defaults to whitespace
///
/// @return returns trimmed string
// --------------------------------------------------------------------------
inline std::string &rtrim(std::string &&str, const std::string &chars = "\t\n\v\f\r ") {
  str.erase(str.find_last_not_of(chars) + 1);
  return str;
}

inline std::string rtrim(const std::string &str, const std::string &chars = "\t\n\v\f\r ") {
  std::string ret{str};

  ret.erase(str.find_last_not_of(chars) + 1);
  return ret;
}

// --------------------------------------------------------------------------
/// @brief removes given chars from beginning and end of string
///
/// @param str   string to be trimmed
/// @param chars characters to be removed, defaults to whitespace
///
/// @return returns trimmed string
// --------------------------------------------------------------------------
inline std::string &trim(std::string &&str, const std::string &chars = "\t\n\v\f\r ") {
  return ltrim(rtrim(str, chars), chars);
}

inline std::string trim(const std::string &str, const std::string &chars = "\t\n\v\f\r ") {
  return ltrim(rtrim(str, chars), chars);
}

// --------------------------------------------------------------------------
/// @brief checks beginning of string1 for presens of string2
///
/// @param str   string to be examined
/// @param start string to be searched
///
/// @return returns true if str begins with startm false otherwise
// --------------------------------------------------------------------------
inline bool starts_with(const std::string &str, const std::string &start) {
  return str.compare(0, start.length(), start) == 0;
}

// --------------------------------------------------------------------------
/// @brief checks end of string 1 for presens of string 2
///
/// @param str   string to be examined
/// @param end   string to be searched
///
/// @return returns true if str begins with startm false otherwise
// --------------------------------------------------------------------------
inline bool ends_with(const std::string &str, const std::string &end) {
  if (str.length() < end.length()) return false;
  return str.compare(str.length() - end.length(), end.length(), end) == 0;
}

inline void ReplaceStringInPlace(std::string &subject, const std::string &search,
                                 const std::string &replace) {
  size_t pos = 0;
  while ((pos = subject.find(search, pos)) != std::string::npos) {
    subject.replace(pos, search.length(), replace);
    pos += replace.length();
  }
}

// a smarter subclass that remembers current location and attempt to do
// relative movement to improve speed. but offer no access to underlying break iterator.
class EGCSmartIterator : public EGCIterator {
 public:
  EGCSmartIterator(const char *src, int32_t slen) : EGCIterator(src, slen) {}
  size_t count() {
    if (count_ == -1) {
      count_ = 0;
      brk_->first();
      while (brk_->next() != icu::BreakIterator::DONE) ++count_;
      current_idx_ = count_;
    }
    return count_;
  }
  int32_t index_to_offset(int32_t index) {
    if (index == 0) {
      current_idx_ = 0;
      return brk_->first();
    }
    if (index == -1) {
      current_idx_ = -1;
      brk_->last();
      return brk_->previous();
    }
    if (index > 0) {  // forward search
      if (current_idx_ < 0 || (current_idx_ >= 0 && current_idx_ > 2 * index)) {
        current_idx_ = 0;
        brk_->first();
      }
    } else {  // reverse search
      if (current_idx_ >= 0 || (current_idx_ < 0 && current_idx_ < 2 * index)) {
        current_idx_ = -1;
        brk_->last();
        brk_->previous();
      }
    }
    auto oldpos = brk_->current();
    auto pos = brk_->next(index - current_idx_);
    if (pos == icu::BreakIterator::DONE) {
      brk_->isBoundary(oldpos);  // reset back
    } else {
      current_idx_ = index;
    }
    return pos;
  }
  int32_t post_index_to_offset(int32_t index) {
    auto pos = index_to_offset(index);
    if (pos < 0) return pos;
    pos = brk_->next();
    brk_->previous();
    return pos;
  }
  int32_t first() {
    current_idx_ = 0;
    return brk_->first();
  }
  int32_t last() {
    current_idx_ = -1;
    return brk_->last();
  }
  int32_t next() {
    auto oldpos = brk_->current();
    auto pos = brk_->next();
    if (pos == icu::BreakIterator::DONE) {
      brk_->isBoundary(oldpos);  // reset
    } else {
      current_idx_++;
    }
    return pos;
  }

 private:
  int32_t current_idx_ = 0;
  int32_t count_ = -1;
};

// Check string s is valid utf8
bool u8_validate(char **);
bool u8_validate(const char *);
bool u8_validate(const uint8_t *, size_t);
UChar32 u8_egc_index_as_single_codepoint(const char *, int32_t, int32_t);
void u8_copy_and_replace_codepoint_at(EGCSmartIterator &iter, char *dst, int32_t index, UChar32 c);
int32_t u8_offset_to_egc_index(EGCIterator &iter, int32_t offset);
int32_t u8_strncpy(uint8_t *, const uint8_t *, const int32_t);
size_t u8_truncate(const uint8_t *, size_t);
// Return display width for string piece, len could be -1 for NULL terminated string.
size_t u8_width(const char *src, int len);
void u8_truncate_below_width(const char *src, size_t len, size_t max_width, bool break_for_line,
                             bool always_break_before_newline, size_t *out_len, size_t *out_width);
std::string u8_sanitize(std::string_view src);
int32_t u8_egc_find_as_offset(EGCIterator &iter, const char *needle, size_t needle_len,
                              bool reverse);

std::vector<std::string_view> u8_egc_split(const char *src, int32_t slen);
std::string u8_convert_encoding(UConverter *trans, const char *data, int len);
#endif  // STRUTILS_H

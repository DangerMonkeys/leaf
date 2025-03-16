#ifndef GPX_PARSER_H
#define GPX_PARSER_H

#include "files.h"
#include "gpx.h"

enum class ReadTagNameResult {
  TagClosed,
  TagOpen,
  Error,
};

/// @brief Parses select GPX content from a provided FileReader
/// @details File may not contain any 0-value byes; these will be considered the end of the file.
/// @details Only literals may appear between <ele> and <name> tags; no additional subtags are
/// allowed.
/// @details No self-closing tags are allowed inside <wpt> or <rtept> tags.
class GPXParser {
 public:
  GPXParser(FileReader* file_reader) {
    _file_reader = file_reader;
    _line = 1;
    _col = 1;
    _error = "";
  }

  bool parse(Navigator* result);

  inline String error() { return _error; }

  inline uint16_t line() { return _line; }

  inline uint16_t col() { return _col; }

 private:
  /// @brief Get next character from FileReader
  /// @return Next character, or 0 if there is no next character
  char getNextChar();

  /// @brief Read characters until the specified boundary of a tag is found
  /// @return True if the specified boundary was found, false if boundary was not found
  bool scrollToTagBoundary(char boundary);

  /// @brief Read just the name of a tag into the provided value buffer, ensuring null termination
  ReadTagNameResult readTagName(char* value);

  /// @brief Read characters until encountering a non-whitespace character
  /// @return First non-whitespace character encountered
  char skipWhitespace();

  /// @brief Read the data from a wpt or rtept tag into the provided waypoint
  /// @details Must start inside the wpt/rtept tag just after the tag name
  bool readWaypoint(Waypoint* waypoint, const char* tag_name);

  /// @brief  Read the key and value of an attribute
  /// @details Routine may start at whitespace before the attribute
  bool readAttribute(char* key, char* value);

  /// @brief Read a literal value located between two tags
  /// @details Leaves cursor at first character of next tag (past opening < character)
  bool readLiteral(char* value);

  /// @brief Starting outside a tag, read up to and through an entire tag and set `key` to the name
  /// of the tag
  bool readFullTagName(char* key);

  /// @brief Read the data from a rte tag into the provided `route`
  /// @details Must start outside the end of the opening rte tag
  bool readRoute(Route* route);

  FileReader* _file_reader;
  uint16_t _line;
  uint16_t _col;
  String _error;
};

#endif

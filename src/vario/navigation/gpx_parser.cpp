#include "navigation/gpx_parser.h"

#include "storage/files.h"

// The maximum length of a value in a GPX (tag name, latitude, longitude, elevation, etc)
#define MAX_VALUE_LENGTH (64)

inline bool isWhitespace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

const char* localTagName(const char* value) {
  const char* name = value[0] == '/' ? value + 1 : value;
  const char* colon = strchr(name, ':');
  return colon ? colon + 1 : name;
}

bool equalsIgnoreCase(char* value, const char* constant) {
  uint16_t i = 0;
  while (i < MAX_VALUE_LENGTH) {
    if (value[i] == 0 && constant[i] == 0) {
      return true;
    }
    if (value[i] != constant[i]) {
      bool lcase_value = 'a' <= value[i] && value[i] <= 'z';
      bool ucase_value = 'A' <= value[i] && value[i] <= 'Z';
      bool lcase_const = 'a' <= constant[i] && constant[i] <= 'z';
      bool ucase_const = 'A' <= constant[i] && constant[i] <= 'Z';
      if (lcase_value && ucase_const) {
        if (value[i] != constant[i] + ('a' - 'A')) {
          return false;
        }
      } else if (ucase_value && lcase_const) {
        if (value[i] + ('a' - 'A') != constant[i]) {
          return false;
        }
      } else {
        return false;
      }
    }
    i++;
  }
  return true;
}

bool tagEqualsIgnoreCase(char* value, const char* constant);
bool closingTagEqualsIgnoreCase(char* value, const char* constant);

bool GPXParser::parse(Navigator* result) {
  result->clear();

  char value_buffer[MAX_VALUE_LENGTH + 1];

  while (scrollToTagBoundary('<')) {
    ReadTagNameResult name_result = readTagName(value_buffer);
    if (name_result == ReadTagNameResult::Error) {
      _error += " while parsing GPX";
      return false;
    }
    if (tagEqualsIgnoreCase(value_buffer, "wpt")) {
      if (name_result == ReadTagNameResult::TagClosed) {
        _error = "missing lat and lon attributes for wpt tag";
        return false;
      }
      Waypoint waypoint;
      if (readWaypoint(&waypoint, "wpt")) {
        if (!result->addOrFindWaypoint(waypoint)) {
          Serial.println("WARNING: maximum number of GPX points reached; skipping extra wpt");
          continue;
        }
      } else {
        _error += " in wpt tag";
        return false;
      }
    } else if (tagEqualsIgnoreCase(value_buffer, "rte")) {
      if (name_result == ReadTagNameResult::TagOpen) {
        if (!scrollToTagEnd()) {
          _error = "reached end of file when reading rte tag";
          return false;
        }
      }
      Route route;
      bool storeRoute = result->totalRoutes < maxRoutes;
      if (readRoute(result, &route, storeRoute)) {
        if (!storeRoute) {
          Serial.println("WARNING: maximum number of GPX routes reached; skipping extra rte");
          continue;
        }
        result->routes[result->totalRoutes + 1] =
            route;  // TODO: changed to add +1 to index, now routes start at 1, not 0.  Change back
                    // if desired later
        result->totalRoutes++;
      } else {
        _error += " in rte tag";
        return false;
      }
    }
  }

  return true;
}

char GPXParser::getNextChar() {
  if (!_file_reader->contentRemaining()) {
    return 0;
  }
  char c = _file_reader->nextChar();
  if ((++_charsReadSinceYield & 0xFF) == 0) {
    yield();
  }
  if (c == '\n') {
    _line++;
    _col = 1;
  } else if (c == '\r') {
    // Do not increment line nor column for carriage returns
  } else {
    _col++;
  }
  return c;
}

bool GPXParser::scrollToTagBoundary(char boundary) {
  char c;
  do {
    c = getNextChar();
    if (c == boundary) {
      return true;
    }
  } while (c != 0);
  return false;
}

bool tagEqualsIgnoreCase(char* value, const char* constant) {
  return equalsIgnoreCase(const_cast<char*>(localTagName(value)), constant);
}

bool closingTagEqualsIgnoreCase(char* value, const char* constant) {
  return value[0] == '/' && tagEqualsIgnoreCase(value, constant);
}

bool GPXParser::scrollToTagEnd() {
  char c;
  char lastNonWhitespace = 0;
  do {
    c = getNextChar();
    if (c == '>') {
      _lastTagSelfClosing = lastNonWhitespace == '/';
      return true;
    }
    if (c != 0 && !isWhitespace(c)) {
      lastNonWhitespace = c;
    }
  } while (c != 0);
  return false;
}

ReadTagNameResult GPXParser::readTagName(char* value) {
  ReadTagNameResult result;
  char c;
  uint16_t i = 0;
  bool name_started = false;
  _lastTagSelfClosing = false;
  while (true) {
    c = getNextChar();
    if (c == '>') {
      if (i > 0 && value[i - 1] == '/') {
        i--;
        _lastTagSelfClosing = true;
      }
      result = ReadTagNameResult::TagClosed;
      break;
    } else if (isWhitespace(c)) {
      if (name_started) {
        result = ReadTagNameResult::TagOpen;
        break;
      }
    } else if (c == 0) {
      _error = "reached end of file while reading tag name";
      result = ReadTagNameResult::Error;
      break;
    } else {
      value[i++] = c;
      name_started = true;
      if (i >= MAX_VALUE_LENGTH) {
        _error = "tag name was too long";
        result = ReadTagNameResult::Error;
        break;
      }
    }
  }
  value[i] = 0;  // Null-terminate the value
  return result;
}

char GPXParser::skipWhitespace() {
  char c;
  do {
    c = getNextChar();
  } while (isWhitespace(c));
  return c;
}

bool GPXParser::readWaypoint(Waypoint* waypoint, const char* tag_name) {
  char key[MAX_VALUE_LENGTH + 1];
  char value[MAX_VALUE_LENGTH + 1];
  waypoint->name = "";
  waypoint->ele = 0;

  // Read attributes of opening tag (until tag is closed)
  bool found_lat = false;
  bool found_lon = false;
  bool self_closing = false;
  while (true) {
    char c = skipWhitespace();
    if (c == 0) {
      _error = "reached end of file while looking for attributes in waypoint tag";
      return false;
    } else if (c == '>') {
      break;
    } else if (c == '/') {
      c = skipWhitespace();
      if (c != '>') {
        _error = "unexpected character after self-closing marker in waypoint tag";
        return false;
      }
      self_closing = true;
      break;
    }
    key[0] = c;
    bool attribute_success = readAttribute(key + 1, value);
    if (!attribute_success) {
      _error += " while reading attribute in waypoint tag";
      return false;
    }
    if (equalsIgnoreCase(key, "lat")) {
      waypoint->lat = atof(value);
      found_lat = true;
    } else if (equalsIgnoreCase(key, "lon")) {
      waypoint->lon = atof(value);
      found_lon = true;
    }
  }
  if (!found_lat) {
    _error = "couldn't find lat attribute in waypoint tag";
    return false;
  }
  if (!found_lon) {
    _error = "couldn't find lon attribute in waypoint tag";
    return false;
  }
  if (self_closing) {
    return true;
  }

  // Look for content or the closing tag
  while (true) {
    // Read next tag
    if (!readFullTagName(key)) {
      _error += " while looking for the closing waypoint tag";
      return false;
    }

    if (closingTagEqualsIgnoreCase(key, tag_name)) {
      // This was the closing tag for the waypoint
      break;
    } else if (tagEqualsIgnoreCase(key, "ele")) {
      // This was an opening elevation tag
      if (!readLiteral(value, false)) {
        _error += " while reading ele value in waypoint";
        return false;
      }
      waypoint->ele = atof(value);
      ReadTagNameResult closing_outcome = readTagName(key);
      if (closing_outcome == ReadTagNameResult::Error) {
        _error += " while reading name of tag that should be a closing ele tag in waypoint";
        return false;
      } else if (closing_outcome == ReadTagNameResult::TagOpen) {
        if (!scrollToTagEnd()) {
          _error = "reached end of file while looking for end of tag after ele in waypoint";
          return false;
        }
      }
      if (!closingTagEqualsIgnoreCase(key, "ele")) {
        _error = "tag after ele in waypoint was not a closing ele tag";
        return false;
      }
    } else if (tagEqualsIgnoreCase(key, "name")) {
      // This was an opening name tag
      if (!readLiteral(value, true)) {
        _error += " while reading value of name tag in waypoint";
        return false;
      }
      waypoint->name = String(value);
      ReadTagNameResult closing_outcome = readTagName(key);
      if (closing_outcome == ReadTagNameResult::Error) {
        _error += " while reading name of tag that should be a closing name tag in waypoint";
        return false;
      } else if (closing_outcome == ReadTagNameResult::TagOpen) {
        if (!scrollToTagEnd()) {
          _error = "reached end of file while looking for end of tag after name in waypoint";
          return false;
        }
      }
      if (!closingTagEqualsIgnoreCase(key, "name")) {
        _error = "tag after name in waypoint was not a closing name tag";
        return false;
      }
    } else {
      // This was an irrelevant tag; look for its closing tag
      if (_lastTagSelfClosing) {
        continue;
      }
      while (true) {
        if (!readFullTagName(value)) {
          _error +=
              " while reading name of tag that should be a closing irrelevant tag in waypoint";
          return false;
        }
        if (value[0] == '/' && strncmp(key, value + 1, MAX_VALUE_LENGTH - 1) == 0) {
          // This was the closing tag for the irrelevant tag within the waypoint; proceed with
          // parsing
          break;
        }
      }
    }
  }

  return true;
}

bool GPXParser::readAttribute(char* key, char* value) {
  // Read key
  uint16_t i = 0;
  char c = skipWhitespace();
  while (true) {
    if (c == 0) {
      _error = "reached end of file while looking for attribute in tag";
      return false;
    } else if (c == '=') {
      break;
    } else if (c == '>') {
      _error = "couldn't find attribute value before end of tag";
      return false;
    } else if (isWhitespace(c)) {
      c = skipWhitespace();
      if (c == '=') {
        break;
      } else {
        _error = "couldn't find attribute value before end of tag";
        return false;
      }
    } else {
      key[i++] = c;
      if (i >= MAX_VALUE_LENGTH) {
        _error = "attribute key was too long";
        return false;
      }
      c = getNextChar();
    }
  }
  key[i] = 0;

  // Read value
  i = 0;
  c = skipWhitespace();
  if (c != '"') {
    _error = "attribute value did not start with an opening quote mark";
    return false;
  }
  while (true) {
    c = getNextChar();
    if (c == 0) {
      _error = "reached end of file while looking for end of attribute value in tag";
      return false;
    } else if (c == '"') {
      value[i] = 0;
      return true;
    } else {
      value[i++] = c;
      if (i >= MAX_VALUE_LENGTH) {
        _error = "attribute value was too long";
        return false;
      }
    }
  }
}

bool GPXParser::readLiteral(char* value, bool truncate) {
  uint16_t i = 0;
  while (true) {
    char c = getNextChar();
    if (c == '<') {
      break;
    } else if (c == 0) {
      _error = "reached end of file while reading literal value";
      return false;
    }
    if (i < MAX_VALUE_LENGTH) {
      value[i++] = c;
    } else if (!truncate) {
      _error = "literal value was too long";
      return false;
    }
  }
  value[i] = 0;
  return true;
}

bool GPXParser::readFullTagName(char* key) {
  if (!scrollToTagBoundary('<')) {
    _error = "reached end of file while reading tag name";
    return false;
  }
  ReadTagNameResult name_outcome = readTagName(key);
  if (name_outcome == ReadTagNameResult::Error) {
    return false;
  } else if (name_outcome == ReadTagNameResult::TagOpen) {
    if (!scrollToTagEnd()) {
      _error = "reached end of file while looking for end of tag";
      return false;
    }
  }
  return true;
}

bool GPXParser::readRoute(Navigator* result, Route* route, bool storePoints) {
  route->firstRoutePointIndex = 0;
  route->totalPoints = 0;

  char key[MAX_VALUE_LENGTH + 1];
  char value[MAX_VALUE_LENGTH + 1];

  // Look for content or the closing tag
  while (true) {
    // Find next tag
    if (!scrollToTagBoundary('<')) {
      _error = "reached end of file while reading tags in route";
      return false;
    }

    // Read next tag
    ReadTagNameResult name_outcome = readTagName(key);
    if (name_outcome == ReadTagNameResult::Error) {
      _error += " while reading tag in route";
      return false;
    }

    // For every tag except rtept, scroll to end of tag
    if (!tagEqualsIgnoreCase(key, "rtept") && name_outcome == ReadTagNameResult::TagOpen &&
        !scrollToTagEnd()) {
      _error = "reached end of file while looking for end of tag in route";
      return false;
    }

    if (closingTagEqualsIgnoreCase(key, "rte")) {
      // This was the closing tag for the route
      break;
    } else if (tagEqualsIgnoreCase(key, "rtept")) {
      // This is an opening route point tag
      Waypoint waypoint;
      if (!readWaypoint(&waypoint, "rtept")) {
        _error = " while reading rtept";
        return false;
      }
      if (!storePoints) {
        continue;
      }
      if (result->totalRoutePointRefs >= maxRoutePointRefs) {
        Serial.println("WARNING: maximum number of GPX route point refs reached; skipping rtept");
        continue;
      }
      WaypointID waypointIndex = result->addOrFindWaypoint(waypoint);
      if (!waypointIndex) {
        Serial.println("WARNING: maximum number of GPX waypoints reached; skipping extra rtept");
        continue;
      }
      if (!result->addRoutePoint(route, waypointIndex)) {
        Serial.println("WARNING: maximum number of GPX route point refs reached; skipping rtept");
        continue;
      }
    } else if (tagEqualsIgnoreCase(key, "name")) {
      // This is an opening name tag
      if (!readLiteral(value, true)) {
        _error += " while reading route name";
        return false;
      }
      route->name = String(value);
      ReadTagNameResult closing_outcome = readTagName(key);
      if (closing_outcome == ReadTagNameResult::Error) {
        _error += " while reading closing name tag in route";
        return false;
      } else if (closing_outcome == ReadTagNameResult::TagOpen) {
        if (!scrollToTagEnd()) {
          _error =
              "reached end of file while reading name of tag that should be a closing name tag in "
              "route";
          return false;
        }
      }
      if (!closingTagEqualsIgnoreCase(key, "name")) {
        _error = "missing closing name tag in route";
        return false;
      }
    } else {
      // This was an irrelevant tag; look for its closing tag
      if (_lastTagSelfClosing) {
        continue;
      }
      while (true) {
        if (!readFullTagName(value)) {
          _error += " while reading the name of a closing tag for an irrelevant tag in a route";
          return false;
        }
        if (value[0] == '/' && strncmp(key, value + 1, MAX_VALUE_LENGTH - 1) == 0) {
          // This was the closing tag for the irrelevant tag within the route; proceed with parsing
          break;
        }
      }
    }
  }

  return true;
}

#include "gpx_parser.h"

#include "files.h"

// The maximum length of a value in a GPX (tag name, latitude, longitude, elevation, etc)
#define MAX_VALUE_LENGTH (32)

inline bool isWhitespace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

bool equalsIgnoreCase(char* value, const char* constant) {
  uint16_t i = 0;
  while (i < MAX_VALUE_LENGTH) {
    if (value[i] == 0 && constant[i] == 0) {
      return true;
    }
    if (value[i] != constant[i]) {
      bool lcase_value = 'a' <= value[i] && value[i] <= 'z';
      bool ucase_value = 'A' <= value[i] && value[i] <= 'Z';
      bool lcase_const = 'a' <= constant[i] && constant[i] <= 'A';
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

bool GPXParser::parse(Navigator* result) {
  result->totalRoutes = 0;
  result->totalWaypoints = 0;

  char value_buffer[MAX_VALUE_LENGTH + 1];

  while (scrollToTagBoundary('<')) {
    ReadTagNameResult name_result = readTagName(value_buffer);
    if (name_result == ReadTagNameResult::Error) {
      _error += " while parsing GPX";
      return false;
    }
    if (equalsIgnoreCase(value_buffer, "wpt")) {
      if (name_result == ReadTagNameResult::TagClosed) {
        _error = "missing lat and lon attributes for wpt tag";
        return false;
      }
      if (result->totalWaypoints >= maxWaypoints) {
        _error = "maximum number of GPX waypoints exceeded";
        return false;
      }
      Waypoint waypoint;
      if (readWaypoint(&waypoint, "wpt")) {
        result->waypoints[result->totalWaypoints + 1] =
            waypoint;  // TODO: changed to add +1 to index, now waypoints start at 1, not 0.  Change
                       // back if desired later
        result->totalWaypoints++;
      } else {
        _error += " in wpt tag";
        return false;
      }
    } else if (equalsIgnoreCase(value_buffer, "rte")) {
      if (name_result == ReadTagNameResult::TagOpen) {
        if (!scrollToTagBoundary('>')) {
          _error = "reached end of file when reading rte tag";
          return false;
        }
      }
      if (result->totalRoutes >= maxRoutes) {
        _error = "maximum number of GPX routes exceeded";
        return false;
      }
      Route route;
      if (readRoute(&route)) {
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

ReadTagNameResult GPXParser::readTagName(char* value) {
  ReadTagNameResult result;
  char c;
  uint16_t i = 0;
  bool name_started = false;
  while (true) {
    c = getNextChar();
    if (c == '>') {
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

  // Read attributes of opening tag (until tag is closed)
  bool found_lat = false;
  bool found_lon = false;
  while (true) {
    char c = skipWhitespace();
    if (c == 0) {
      _error = "reached end of file while looking for attributes in waypoint tag";
      return false;
    } else if (c == '>') {
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

  // Look for content or the closing tag
  while (true) {
    // Read next tag
    if (!readFullTagName(key)) {
      _error += " while looking for the closing waypoint tag";
      return false;
    }

    if (key[0] == '/' && equalsIgnoreCase(key + 1, tag_name)) {
      // This was the closing tag for the waypoint
      break;
    } else if (equalsIgnoreCase(key, "ele")) {
      // This was an opening elevation tag
      if (!readLiteral(value)) {
        _error += " while reading ele value in waypoint";
        return false;
      }
      waypoint->ele = atof(value);
      ReadTagNameResult closing_outcome = readTagName(key);
      if (closing_outcome == ReadTagNameResult::Error) {
        _error += " while reading name of tag that should be a closing ele tag in waypoint";
        return false;
      } else if (closing_outcome == ReadTagNameResult::TagOpen) {
        if (!scrollToTagBoundary('>')) {
          _error = "reached end of file while looking for end of tag after ele in waypoint";
          return false;
        }
      }
      if (!equalsIgnoreCase(key, "/ele")) {
        _error = "tag after ele in waypoint was not a closing ele tag";
        return false;
      }
    } else if (equalsIgnoreCase(key, "name")) {
      // This was an opening name tag
      if (!readLiteral(value)) {
        _error += " while reading value of name tag in waypoint";
        return false;
      }
      waypoint->name = String(value);
      ReadTagNameResult closing_outcome = readTagName(key);
      if (closing_outcome == ReadTagNameResult::Error) {
        _error += " while reading name of tag that should be a closing name tag in waypoint";
        return false;
      } else if (closing_outcome == ReadTagNameResult::TagOpen) {
        if (!scrollToTagBoundary('>')) {
          _error = "reached end of file while looking for end of tag after name in waypoint";
          return false;
        }
      }
      if (!equalsIgnoreCase(key, "/name")) {
        _error = "tag after name in waypoint was not a closing name tag";
        return false;
      }
    } else {
      // This was an irrelevant tag; look for its closing tag
      while (true) {
        if (!readFullTagName(value)) {
          _error +=
              " while reading name of tag that should be a closing irrelevant tag in waypoint";
          return false;
        }
        if (value[0] == '/' && strncmp(key, value + 1, MAX_VALUE_LENGTH - 1)) {
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

bool GPXParser::readLiteral(char* value) {
  uint16_t i = 0;
  while (true) {
    char c = getNextChar();
    if (c == '<') {
      break;
    } else if (c == 0) {
      _error = "reached end of file while reading literal value";
      return false;
    }
    value[i++] = c;
    if (i >= MAX_VALUE_LENGTH) {
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
  if (name_outcome == ReadTagNameResult::TagOpen) {
    if (!scrollToTagBoundary('>')) {
      _error = "reached end of file while looking for end of tag";
      return false;
    }
  } else if (name_outcome == ReadTagNameResult::TagClosed) {
    return true;
  }
  return false;
}

bool GPXParser::readRoute(Route* route) {
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
    if (!equalsIgnoreCase(key, "rtept") && name_outcome == ReadTagNameResult::TagOpen &&
        !scrollToTagBoundary('>')) {
      _error = "reached end of file while looking for end of tag in route";
      return false;
    }

    if (equalsIgnoreCase(key, "/rte")) {
      // This was the closing tag for the route
      break;
    } else if (equalsIgnoreCase(key, "rtept")) {
      // This is an opening route point tag
      if (route->totalPoints >= maxRoutePoints) {
        _error = "maximum number of route points exceeded";
        return false;
      }
      Waypoint waypoint;
      if (!readWaypoint(&waypoint, "rtept")) {
        _error = " while reading rtept";
        return false;
      }
      route->routepoints[route->totalPoints + 1] =
          waypoint;  // TODO: changed to add +1 to index, now routepoints start at 1, not 0.  Change
                     // back if desired later
      route->totalPoints++;
    } else if (equalsIgnoreCase(key, "name")) {
      // This is an opening name tag
      if (!readLiteral(value)) {
        _error += " while reading route name";
        return false;
      }
      route->name = String(value);
      ReadTagNameResult closing_outcome = readTagName(key);
      if (closing_outcome == ReadTagNameResult::Error) {
        _error += " while reading closing name tag in route";
        return false;
      } else if (closing_outcome == ReadTagNameResult::TagOpen) {
        if (!scrollToTagBoundary('>')) {
          _error =
              "reached end of file while reading name of tag that should be a closing name tag in "
              "route";
          return false;
        }
      }
      if (!equalsIgnoreCase(key, "/name")) {
        _error = "missing closing name tag in route";
        return false;
      }
    } else {
      // This was an irrelevant tag; look for its closing tag
      while (true) {
        if (!readFullTagName(value)) {
          _error += " while reading the name of a closing tag for an irrelevant tag in a route";
          return false;
        }
        if (value[0] == '/' && strncmp(key, value + 1, MAX_VALUE_LENGTH - 1)) {
          // This was the closing tag for the irrelevant tag within the route; proceed with parsing
          break;
        }
      }
    }
  }

  return true;
}

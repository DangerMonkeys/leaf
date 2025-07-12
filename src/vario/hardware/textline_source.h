#pragma once

#include <cstddef>

class ITextLineSource {
 public:
  // Initialize the text line source.  Before this method is called, none of the other
  // methods are expected to behave correctly.  After this method completes, all of
  // the other methods are expected to behave correctly.
  virtual void init() = 0;

  // Call this method as frequently as desired to perform text acquisition tasks.
  // The return result indicates if a new line of text has been acquired.
  virtual bool update() = 0;

  // Get line of text indicated as available by update.
  virtual const char* getTextLine() = 0;

  virtual ~ITextLineSource() = default;  // Always provide a virtual destructor
};

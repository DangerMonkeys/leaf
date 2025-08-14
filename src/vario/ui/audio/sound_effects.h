#pragma once

#include <cstdint>

#include "ui/audio/notes.h"

// Sound Effects
namespace fx {
  using note::note_t;

  constexpr note_t silence[] = {note::END};

  constexpr note_t increase[] = {note::C4, note::G4, note::END};
  constexpr note_t decrease[] = {note::C4, note::F3, note::END};  // 110 140, END_OF_TONE};
  constexpr note_t neutral[] = {note::C4, note::C4, note::END};   // 110, 110, END_OF_TONE};
  constexpr note_t neutralLong[] = {
      note::C4, note::C4, note::C4, note::C4, note::C4,
      note::C4, note::C4, note::C4, note::END};  //  {110, 110, 110, 110, 110, 110, 110, 110,10,
                                                 //  110, 1110, 110, 110, 110, 110, 110, 110, 110,
                                                 //  110, 110, END_OF_TONE};
  constexpr note_t doubleClick[] = {note::C4, note::NONE, note::C4,
                                    note::END};  // 110, 0, 110, END_OF_TONE};

  constexpr note_t enter[] = {note::A4, note::C4, note::E4,
                              note::END};  // 150, 120, 90, END_OF_TONE};
  constexpr note_t exit[] = {note::C5, note::A5, note::F4, note::C4,
                             note::END};  // 65, 90, 120, 150, END_OF_TONE};
  constexpr note_t confirm[] = {note::C3, note::G3, note::B3, note::C4, note::END};
  constexpr note_t cancel[] = {note::C4, note::G4, note::C3, note::END};
  constexpr note_t on[] = {250, 200, 150, 100, 50, note::END};
  constexpr note_t off[] = {50, 100, 150, 200, 250, note::END};

  constexpr note_t buttonpress[] = {note::F3, note::A3, note::C4, note::END};
  constexpr note_t buttonhold[] = {150, 200, 250, note::END};
  constexpr note_t goingup[] = {55, 54, 53, 52, 51, 50, 49, 47, 44, 39, note::END};
  constexpr note_t goingdown[] = {31, 31, 32, 33, 34, 35, 36, 38, 41, 46, note::END};
  constexpr note_t octavesup[] = {45, 44, 43, 42, 41, 40, 39, 38,
                                  37, 36, 35, 34, 33, 32, 31, note::END};
  constexpr note_t octavesdown[] = {31, 31, 40, 40, 45, 45, 65, 65, 90, 90, note::END};

  constexpr const note_t* started =
      buttonhold;  // TODO: Make a more distinctive "thing started" sound

  constexpr note_t fatalerror[] = {50, 250, 50, 250, 50, 250, 50, 250, 50, 250, note::END};
  constexpr note_t bad[] = {50, 250, 50, 250, note::END};
}  // namespace fx

# Navigation pointer fixture

`nav-pointers-flat-mask.txt` pins the 15 × 16 native LCD crop around the
fixed heading marker and active direction arrow. The first line gives
the crop's x, y, width and height. A `#` marks a dark pixel and a `.` marks
background.

`make -C sim test-route-guidance` compares the straight-ahead LCD render
with this mask and checks that the left-pointing arrow leaves the vario bar
untouched. Rotated renders are written to the ignored test build directory.

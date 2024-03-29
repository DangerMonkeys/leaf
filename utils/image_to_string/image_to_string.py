import argparse
import math

import imageio.v2 as imageio
from numpy.typing import NDArray
import numpy as np


PIXELS_PER_CHAR = 8


def make_monochrome(img: NDArray) -> NDArray[bool]:
    if img.dtype == np.uint8 and len(img.shape) == 3:
        # This is a standard 3-byte-per-pixel RGB image
        return np.dot(img[..., :3], [0.2989, 0.5870, 0.1140]) < 128
    else:
        raise NotImplementedError(f"Cannot convert {img.dtype} image with {len(img.shape)} dimensions to monochrome")


def string_of_image(img: NDArray[bool], max_line_length: int) -> list[str]:
    lines = []
    line = ""
    for page in range(math.ceil(img.shape[1] / 8)):
        for col in range(img.shape[0]):
            value = 0
            for u in range(PIXELS_PER_CHAR):
                value <<= 1
                if img[col, (page + 1) * PIXELS_PER_CHAR - u - 1]:
                    value += 1
            new_content = f"\\x{value:02x}"
            if len(line) + len(new_content) > max_line_length:
                lines.append(line)
                line = new_content
            else:
                line += new_content
    if line:
        lines.append(line)
    return lines


def main():
    parser = argparse.ArgumentParser(description="Convert an image into a compact string that can be written to a monochrome LCD")
    parser.add_argument(
        "--image",
        help="Path to image to read",
        type=str)
    parser.add_argument(
        "--characters-per-line",
        help="Maximum number of characters per line",
        type=int,
        default=78,
    )
    args = parser.parse_args()

    img = imageio.imread(args.image)
    monochrome = make_monochrome(img)
    lines = string_of_image(monochrome, args.characters_per_line)
    for i, line in enumerate(lines):
        if i == 0:
            prefix = 'String s = "'
        else:
            prefix = '"'
        if i == len(lines) - 1:
            suffix = '";'
        else:
            suffix = '"'
        print(prefix + line + suffix)


if __name__ == '__main__':
    main()

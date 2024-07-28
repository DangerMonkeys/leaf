# VS Code support

The folder contains the information and tools to support development of this project in VS Code.

## VS Code setup

To develop this project in VS Code, follow these setup steps:

* Install Arduino and all prerequisites and verify that the project can be built and uploaded via the Arduino IDE (see [documentation](../vario/README.md))
* Ensure that the [C/C++ extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools) is installed
* Configure [c_cpp_properties.json](c_cpp_properties.json) in this folder by running [configure_vscode.py](configure_vscode.py) starting the working directory of [`src`](..) (one level higher than this folder)
    * Note: see [reference for this file](https://code.visualstudio.com/docs/cpp/c-cpp-properties-schema-reference)
* Open this folder as a project

Reference: https://learn.sparkfun.com/tutorials/efficient-arduino-programming-with-arduino-cli-and-visual-studio-code/all

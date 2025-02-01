
# Style Guide
A style guide to advise on new code until a code cleanup is done and everything is consistent.

# C++ Style Guide (Google Style)

## 1. Class and Struct Names
- **Use CamelCase** (capitalizing the first letter of each word) for class and struct names.
- The name should be descriptive and convey the purpose of the class.
- **Example**:
    ```cpp
    class MyClass {
    public:
        // class contents
    };
    
    struct MyStruct {
    public:
        // struct contents
    };
    ```

## 2. Function and Method Names
- **Use camelCase** (lowercase for the first word, uppercase for subsequent words) for function and method names.
- Start with a verb to indicate an action or behavior.
- **Example**:
    ```cpp
    void doSomething() {
        // code
    }

    int calculateSum(int a, int b) {
        return a + b;
    }
    ```

## 3. Variables
- **Use camelCase** for variable names. Begin with a lowercase letter and capitalize the first letter of each subsequent word.
- Keep variable names descriptive but concise. Avoid overly long names.
- **Example**:
    ```cpp
    int itemCount;
    double totalAmount;
    bool isValid;
    ```

## 4. Constant Variables
- **Use UPPERCASE_WITH_UNDERSCORES** for constant variables. Constants are usually global or static values that do not change during the program's execution.
- Typically, you should declare constants with `const` or `constexpr` in C++.
- **Example**:
    ```cpp
    const int MAX_BUFFER_SIZE = 1024;
    constexpr double PI = 3.14159;
    ```

## 5. Macro Names
- **Use UPPERCASE_WITH_UNDERSCORES** for macros. Macros are constants or functions that are replaced by the preprocessor before compilation.
- Ensure macro names are descriptive of their purpose or use.
- **Example**:
    ```cpp
    #define MAX_LENGTH 100
    #define SQUARE(x) ((x) * (x))
    ```

## 6. File Names
- **Use lowercase_with_underscores** for file names, particularly in multi-platform code.
- This makes it easy to read and avoid issues with case-sensitivity on different systems.
- **Example**:
    ```
    my_class.cpp
    my_header_file.h
    data_processor.cpp
    ```

## 7. Private Attributes (Member Variables)
- **Use m_** as a prefix followed by **camelCase** for private member variables.
- The prefix `m_` indicates that the variable is a member of the class.
- **Example**:
    ```cpp
    class MyClass {
    private:
        int m_itemCount;
        double m_totalAmount;
    public:
        MyClass() : m_itemCount(0), m_totalAmount(0.0) {}
    };
    ```

## 8. Global Variables
- **Use g_** as a prefix for global variables, followed by **camelCase**.
- Global variables should be avoided unless absolutely necessary, as they can lead to poor maintainability and potential conflicts.
- **Example**:
    ```cpp
    int g_totalAmount;
    ```

## 9. Enumerations
- **Use CamelCase** for enumeration type names and **UPPERCASE_WITH_UNDERSCORES** for enumeration constants.
- The naming of the enum type should follow the same pattern as class names, while the constants should be all caps with underscores.
- **Example**:
    ```cpp
    enum class FileMode {
        READ_ONLY,
        WRITE_ONLY,
        READ_WRITE
    };
    ```

## 10. Type Aliases (typedef or using)
- **Use camelCase** for type alias names.
- Avoid using underscores in type alias names.
- **Example**:
    ```cpp
    using IntList = std::vector<int>;
    using StringMap = std::unordered_map<std::string, std::string>;
    ```

## 11. Namespace Names
- **Use camelCase** for namespaces. Make sure it reflects the project or module it belongs to.
- **Example**:
    ```cpp
    namespace myProject {
        class MyClass {
            // class code
        };
    }
    ```

## 12. Test Functions and Variables
- **Use test_** as a prefix for unit test functions and variables.
- For test-related functions, also use **camelCase**.
- **Example**:
    ```cpp
    void testCalculateSum() {
        // test logic
    }
    
    int testItemCount;
    ```

## 13. Parameters and Arguments
- **Use camelCase** for function parameters and argument names.
- Keep parameters and arguments descriptive but concise.
- **Example**:
    ```cpp
    void processOrder(int orderId, const std::string& customerName) {
        // process order logic
    }
    ```

## 14. Template Parameter Names
- **Use single uppercase letters** for template parameter names (e.g., T, U, V).
- When the type is more specific, use descriptive names, but still in **CamelCase**.
- **Example**:
    ```cpp
    template <typename T>
    void addElement(T element) {
        // code
    }
    ```

## 15. Exceptions
- **Use camelCase** for exception class names.
- In the event that the exception name reflects an error type, you might use a descriptive name that ends with "Exception".
- **Example**:
    ```cpp
    class InvalidArgumentException : public std::exception {
        const char* what() const noexcept override {
            return "Invalid argument provided!";
        }
    };
    ```

---

## General Guidelines
- **Consistency** is key. Stick to the conventions for readability and maintainability.
- **Descriptive names** are essential, but avoid overly long names.
- **Avoid abbreviations** unless they are widely understood (e.g., `int numItems` is fine, but `int nI` is not).
- **Prefixes for member variables**, global variables, and others make it easier to understand their scope at a glance.

#include <iostream>

struct cat {
    const char* name;
    void meow() const { std::cout << "meow\n"; }
};

int main() {
    // Define a member pointer called data_ptr that can point to any data member
    // of cat whose type is const char*, and initialize it to point to
    // cat::name.
    const char*(cat::*data_ptr) = &cat::name;

    // A member function pointer called func_ptr that can point to any member
    // function of cat so long as it takes no arguments and returns void. We
    // initialize the pointer to cat::meow.
    void (cat::*func_ptr)() const = &cat::meow;

    cat marshmallow{"Marshmallow"};
    auto name = marshmallow.*data_ptr;
    (marshmallow.*func_ptr)();

    return 0;
}
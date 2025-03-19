#include <iostream>

int libmeow_client_cuteness = 100;
bool libmeow_client_is_cute();

int main() {
    std::cout << "Cuteness rating: " << libmeow_client_cuteness << '\n';
    // boolalpha will make it so that true or false is printed rather than 1 or
    // 0
    std::cout << "Is cute: " << std::boolalpha << libmeow_client_is_cute()
              << '\n';
    return 0;
}

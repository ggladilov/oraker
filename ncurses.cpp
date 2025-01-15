#include <iostream>
#include <locale.h>
#include <ncurses.h>

int main() {
    setlocale(LC_ALL, ""); // Set the locale for wide character support

    // Initialize curses
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    // Enable Unicode support
    if (!has_colors() || !can_change_color()) {
        endwin();
        std::cerr << "Your terminal does not support Unicode characters." << std::endl;
        return 1;
    }
    start_color();
    use_default_colors();

    // Define color pairs
    init_pair(1, COLOR_GREEN, -1); // Green foreground, default background
    init_pair(2, COLOR_RED, -1); // Red foreground, default background
    init_pair(3, COLOR_BLUE, -1); // Blue foreground, default background

    // Print Unicode characters with different colors
    attron(A_BOLD); // Make the text bold for better visibility
    attron(COLOR_PAIR(1));
    mvaddwch(5, 5, L'\u2665'); // Heart in green
    attron(COLOR_PAIR(2));
    mvaddwch(5, 6, L'\u26A0'); // Warning in red
    attron(COLOR_PAIR(3));
    mvaddwch(5, 7, L'\u2728'); // Sparkles in blue
    attroff(COLOR_PAIR(3));
    attroff(COLOR_PAIR(2));
    attroff(COLOR_PAIR(1));
    attroff(A_BOLD);
    refresh();

    // Wait for a key press before exiting
    getch();

    // End curses
    endwin();

    return 0;
}

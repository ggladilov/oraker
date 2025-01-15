#include <iostream>
#define _XOPEN_SOURCE_EXTENDED
#include <curses.h>
#include <unistd.h>

/* colors */
// #define COLOR_BLACK	0
// #define COLOR_RED	1
// #define COLOR_GREEN	2
// #define COLOR_YELLOW	3
// #define COLOR_BLUE	4
// #define COLOR_MAGENTA	5
// #define COLOR_CYAN	6
// #define COLOR_WHITE	7


int main() {
    setlocale(LC_ALL, "");
    auto window = initscr(); // Initialize curses mode

    mvprintw(4, 0, "This is line %d", 4);


    start_color();
    use_default_colors();

    init_pair(1, COLOR_BLACK, COLOR_WHITE);
    init_pair(2, COLOR_RED, COLOR_WHITE);
    init_pair(3, COLOR_GREEN, COLOR_WHITE);
    init_pair(4, COLOR_YELLOW, COLOR_WHITE);
    init_pair(5, COLOR_BLUE, COLOR_WHITE);
    init_pair(6, COLOR_MAGENTA, COLOR_WHITE);
    init_pair(7, COLOR_CYAN, COLOR_WHITE);
    init_pair(8, COLOR_WHITE, COLOR_WHITE);

    wbkgd(window, COLOR_PAIR(8));

    for (std::size_t i = 1; i < 9; ++i) {
        attron(COLOR_PAIR(i));
        mvaddwstr(5 + i, 0, L"♠︎");
        mvaddstr(5 + i, 1, " ");
        attron(A_BOLD);
        mvaddwstr(5 + i, 2, L"♠︎\n");
        attroff(A_BOLD);
        attroff(COLOR_PAIR(i));
    }
    attron(COLOR_PAIR(1));
    mvaddstr(5 + 9, 0, "Testing displaying this text!\n");
    attroff(COLOR_PAIR(1));
    refresh();
    getch();

    // Loop to print and remove lines
    // for(int i = 0; i < 10; ++i) {
    //     mvprintw(i, 0, "This is line %d", i);
    //     refresh();
    //     usleep(500000); // Sleep for 0.5 seconds
    //     move(i, 0); // Move cursor back to the beginning of the line
    //     clrtoeol(); // Clear the line from the current cursor position to the end
    //     refresh(); // Refresh the screen to show the changes
    // }

    // getch(); // Wait for user input
    endwin(); // End curses mode

    return 0;
}

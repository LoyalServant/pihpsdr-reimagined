/*   Copyright (C) 2024 - Mark Rutherford, KB2YCW
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifdef _WIN32
    #include <windows.h>
    static void sleep_ms(unsigned int ms) {
        Sleep(ms);
    }
#else
    #include <unistd.h>
    static void sleep_ms(unsigned int ms) {
        usleep(ms * 1000);  // Convert milliseconds to microseconds
    }
#endif
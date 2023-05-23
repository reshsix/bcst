# This file is part of bcst.

# bcst is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published
# by the Free Software Foundation, version 3.

# bcst is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with bcst. If not, see <https://www.gnu.org/licenses/>.

DESTDIR ?= /usr/local/bin/
CFLAGS += -O2 -Wall -Wextra

.PHONY: all clean

all: bcst
clean:
	rm -f bcst

install: bcst
	install -c bcst $(DESTDIR)
uninstall:
	rm -f $(shell realpath "$(DESTDIR)/bcst")

bcst: main.c
	$(CC) $(CFLAGS) $< -o $@

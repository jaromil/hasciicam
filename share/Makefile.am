Applicationsdir = $(datadir)/applications
Applications_DATA = hasciicam.desktop
# $(datadir)/app-install/desktop

Menudir = $(datadir)/menu
Menu_DATA = hasciicam

Iconsdir = $(datadir)/icons
Icons_DATA = hasciicam.png
# $(datadir)/app-install/icons

install-data-hook:
	@if ! [ -n `which xdg-icon-resource` ]; then \
		xdg-icon-resource install --size 64 hasciicam.png dyne-hasciicam; fi
	@if ! [ -n `which update-desktop-database` ]; then update-desktop-database; fi
	@if ! [ -n `which update-menus` ]; then update-menus; fi


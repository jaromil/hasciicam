Applicationsdir = $(datadir)/applications
Applications_DATA = hasciicam.desktop
# $(datadir)/app-install/desktop

Menudir = $(datadir)/menu
Menu_DATA = hasciicam

Iconsdir = $(datadir)/icons
Icons_DATA = hasciicam.png
# $(datadir)/app-install/icons

install-data-hook:
	xdg-icon-resource install --size 64 hasciicam.png dyne-hasciicam
	update-desktop-database
	update-menus


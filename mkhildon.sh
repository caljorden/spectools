rm -r build-pkg
cp -r pkg-hildon build-pkg 
mkdir -p build-pkg/usr/bin
mkdir -p build-pkg/usr/libexec
strip spectool_hildon usbcontrol
cp spectool_hildon build-pkg/usr/bin/spectool
cp usbcontrol build-pkg/usr/libexec/usbcontrol
for x in `find build-pkg -name .svn`; do
	rm -r $x;
done
fakeroot dpkg -b build-pkg spectool-armel.deb

Name: huptime
Summary: Utility for zero downtime restart
Version: %{version}
Release: %{release}
Group: System
License: Copyright 2013 Adin Scannell
URL: http://github.com/amscanne/huptime
Packager: Adin Scannell <adin@scannell.ca>
BuildRoot: %{_tmppath}/%{name}.%{version}-buildroot
AutoReq: no
AutoProv: no

%global _binary_filedigest_algorithm 1
%define __os_install_post %{nil}

%description
Utility for zero downtime restart of unmodified applications.

%install
true

%files
/usr/bin/huptime
/usr/lib/huptime/huptime.so

%changelog
* Sat Oct 26 2013 Adin Scannell <adin@scannell.ca>
- Initial creation of package.

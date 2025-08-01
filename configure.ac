AC_PREREQ([2.69])
AC_INIT([connman], [1.45])

AC_REQUIRE_AUX_FILE([tap-driver.sh])

AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([foreign subdir-objects color-tests dist-xz])
AC_CONFIG_HEADERS([config.h])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AM_MAINTAINER_MODE

AC_PREFIX_DEFAULT(/usr/local)

PKG_PROG_PKG_CONFIG

COMPILER_FLAGS

AC_SUBST(abs_top_srcdir)
AC_SUBST(abs_top_builddir)

AC_LANG([C])
AC_USE_SYSTEM_EXTENSIONS

AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_CC_PIE
AC_PROG_CC_ASAN
AC_PROG_CC_LSAN
AC_PROG_CC_UBSAN
AC_PROG_INSTALL
AC_PROG_MKDIR_P
AC_PROG_AWK

m4_define([_LT_AC_TAGCONFIG], [])
m4_ifdef([AC_LIBTOOL_TAGS], [AC_LIBTOOL_TAGS([])])

LT_PREREQ(2.2)
LT_INIT([disable-static])

gl_CONFIGMAKE_PREP

AC_ARG_ENABLE(optimization, AS_HELP_STRING([--disable-optimization],
			[disable code optimization through compiler]), [
	if (test "${enableval}" = "no"); then
		CFLAGS="$CFLAGS -O0"
	fi
])

if (test "${enable_optimization}" != "no"); then
	CFLAGS+=" -O2 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2"
fi

AC_ARG_ENABLE(debug, AS_HELP_STRING([--enable-debug],
			[enable compiling with debugging information]), [
	if (test "${enableval}" = "yes" &&
				test "${ac_cv_prog_cc_g}" = "yes"); then
		CFLAGS="$CFLAGS -g"
	fi
])

AC_ARG_ENABLE(pie, AS_HELP_STRING([--enable-pie],
			[enable position independent executables flag]), [
	if (test "${enableval}" = "yes" &&
				test "${ac_cv_prog_cc_pie}" = "yes"); then
		CFLAGS="$CFLAGS -fPIE"
		LDFLAGS="$LDFLAGS -pie"
	fi
])

save_LIBS=$LIBS
AC_CHECK_LIB(asan, _init)
LIBS=$save_LIBS

AC_ARG_ENABLE(asan, AS_HELP_STRING([--enable-asan],
			[enable linking with address sanitizer]), [
	if (test "${enableval}" = "yes" &&
				test "${ac_cv_lib_asan__init}" = "yes" &&
				test "${ac_cv_prog_cc_asan}" = "yes"); then
		CFLAGS="$CFLAGS -fsanitize=address";
		LDFLAGS="$LDFLAGS -fsanitize=address"
	fi
])

save_LIBS=$LIBS
AC_CHECK_LIB(lsan, _init)
LIBS=$save_LIBS

AC_ARG_ENABLE(lsan, AS_HELP_STRING([--enable-lsan],
			[enable linking with leak sanitizer]), [
	if (test "${enableval}" = "yes" &&
				test "${ac_cv_lib_lsan__init}" = "yes" &&
				test "${ac_cv_prog_cc_lsan}" = "yes"); then
		CFLAGS="$CFLAGS -fsanitize=leak";
		LDFLAGS="$LDFLAGS -fsanitize=leak"
	fi
])

save_LIBS=$LIBS
AC_CHECK_LIB(ubsan, _init)
LIBS=$save_LIBS

AC_ARG_ENABLE(ubsan, AS_HELP_STRING([--enable-ubsan],
			[enable linking with undefined behavior sanitizer]), [
	if (test "${enableval}" = "yes" &&
				test "${ac_cv_lib_ubsan__init}" = "yes" &&
				test "${ac_cv_prog_cc_ubsan}" = "yes"); then
		CFLAGS="$CFLAGS -fsanitize=undefined";
		LDFLAGS="$LDFLAGS -fsanitize=undefined"
	fi
])

AC_ARG_ENABLE(hh2serial-gps,
	AS_HELP_STRING([--enable-hh2serial-gps], [enable hh2serial GPS support]),
			[enable_hh2serial_gps=${enableval}], [enable_hh2serial_gps="no"])
AM_CONDITIONAL(HH2SERIAL_GPS, test "${enable_hh2serial_gps}" != "no")
AM_CONDITIONAL(HH2SERIAL_GPS_BUILTIN, test "${enable_hh2serial_gps}" = "builtin")

AC_ARG_WITH(openconnect, AS_HELP_STRING([--with-openconnect=PROGRAM],
        [specify location of openconnect binary]), [path_openconnect=${withval}])

AC_ARG_ENABLE(openconnect,
	AS_HELP_STRING([--enable-openconnect], [enable openconnect support]),
			[enable_openconnect=${enableval}], [enable_openconnect="no"])
if (test "${enable_openconnect}" != "no"); then
	if (test -z "${path_openconnect}"); then
		AC_PATH_PROG(OPENCONNECT, [openconnect], [], $PATH:/sbin:/usr/sbin)
		if (test -z "${OPENCONNECT}"); then
			AC_MSG_ERROR(openconnect binary not found)
		fi
	else
		OPENCONNECT="${path_openconnect}"
		AC_SUBST(OPENCONNECT)
	fi
	PKG_CHECK_MODULES(LIBOPENCONNECT, openconnect >= 8, [],
		AC_MSG_ERROR(openconnect >= 8 is required))
fi
AM_CONDITIONAL(OPENCONNECT, test "${enable_openconnect}" != "no")
AM_CONDITIONAL(OPENCONNECT_BUILTIN, test "${enable_openconnect}" = "builtin")

AC_ARG_WITH(openvpn, AS_HELP_STRING([--with-openvpn=PROGRAM],
        [specify location of openvpn binary]), [path_openvpn=${withval}])

AC_ARG_ENABLE(openvpn,
	AS_HELP_STRING([--enable-openvpn], [enable openvpn support]),
			[enable_openvpn=${enableval}], [enable_openvpn="no"])
if (test "${enable_openvpn}" != "no"); then
	if (test -z "${path_openvpn}"); then
		AC_PATH_PROG(OPENVPN, [openvpn], [], $PATH:/sbin:/usr/sbin)
		if (test -z "${OPENVPN}"); then
			AC_MSG_ERROR(openvpn binary not found)
		fi
	else
		OPENVPN="${path_openvpn}"
		AC_SUBST(OPENVPN)
	fi
fi
AM_CONDITIONAL(OPENVPN, test "${enable_openvpn}" != "no")
AM_CONDITIONAL(OPENVPN_BUILTIN, test "${enable_openvpn}" = "builtin")

AC_ARG_WITH(vpnc, AS_HELP_STRING([--with-vpnc=PROGRAM],
	[specify location of vpnc binary]), [path_vpnc=${withval}])

AC_ARG_ENABLE(vpnc,
	AS_HELP_STRING([--enable-vpnc], [enable vpnc support]),
		[enable_vpnc=${enableval}], [enable_vpnc="no"])
if (test "${enable_vpnc}" != "no"); then
	if (test -z "${path_vpnc}"); then
		AC_PATH_PROG(VPNC, [vpnc], [], $PATH:/sbin:/usr/sbin)
		if (test -z "${VPNC}"); then
			AC_MSG_ERROR(vpnc binary not found)
		fi
	else
		VPNC="${path_vpnc}"
		AC_SUBST(VPNC)
	fi
fi
AM_CONDITIONAL(VPNC, test "${enable_vpnc}" != "no")
AM_CONDITIONAL(VPNC_BUILTIN, test "${enable_vpnc}" = "builtin")

AC_ARG_WITH(l2tp, AS_HELP_STRING([--with-l2tp=PROGRAM],
        [specify location of l2tp binary]), [path_l2tp=${withval}])

AC_ARG_ENABLE(l2tp,
	AS_HELP_STRING([--enable-l2tp], [enable l2tp support]),
			[enable_l2tp=${enableval}], [enable_l2tp="no"])
if (test "${enable_l2tp}" != "no"); then
	if (test -z "${path_l2tp}"); then
		AC_PATH_PROG(L2TP, [xl2tpd], [/usr/sbin/xl2tpd], $PATH:/sbin:/usr/sbin)
	else
		L2TP="${path_l2tp}"
		AC_SUBST(L2TP)
	fi
fi
AM_CONDITIONAL(L2TP, test "${enable_l2tp}" != "no")
AM_CONDITIONAL(L2TP_BUILTIN, test "${enable_l2tp}" = "builtin")

AC_ARG_WITH(pptp, AS_HELP_STRING([--with-pptp=PROGRAM],
        [specify location of pptp binary]), [path_pptp=${withval}])

AC_ARG_ENABLE(pptp,
	AS_HELP_STRING([--enable-pptp], [enable pptp support]),
			[enable_pptp=${enableval}], [enable_pptp="no"])
if (test "${enable_pptp}" != "no"); then
	if (test -z "${path_pptp}"); then
		AC_PATH_PROG(PPTP, [pptp], [/usr/sbin/pptp], $PATH:/sbin:/usr/sbin)
	else
		PPTP="${path_pptp}"
		AC_SUBST(PPTP)
	fi
fi
AM_CONDITIONAL(PPTP, test "${enable_pptp}" != "no")
AM_CONDITIONAL(PPTP_BUILTIN, test "${enable_pptp}" = "builtin")

if (test "${enable_pptp}" != "no" || test "${enable_l2tp}" != "no"); then

	if (test -z "${path_pppd}"); then
		AC_PATH_PROG(PPPD, [pppd], [/usr/sbin/pppd], $PATH:/sbin:/usr/sbin)
	else
		PPPD="${path_pppd}"
		AC_SUBST(PPPD)
	fi
	AC_CHECK_HEADERS(pppd/pppd.h, dummy=yes,
			AC_MSG_ERROR(ppp header files are required))
	AC_CHECK_HEADERS([pppd/chap.h pppd/chap-new.h pppd/chap_ms.h])

	PKG_CHECK_EXISTS([pppd],
	    [AS_VAR_SET([pppd_pkgconfig_support],[yes])])

	PPPD_VERSION=2.4.9
	if test x"$pppd_pkgconfig_support" = xyes; then
	    PPPD_VERSION=`$PKG_CONFIG --modversion pppd`
	fi

	AC_DEFINE_UNQUOTED([PPP_VERSION(x,y,z)],
	    [((x & 0xFF) << 16 | (y & 0xFF) << 8 | (z & 0xFF) << 0)],
	    [Macro to help determine the particular version of pppd])
	PPP_VERSION=$(echo $PPPD_VERSION | sed -e "s/\./\,/g")
	AC_DEFINE_UNQUOTED(WITH_PPP_VERSION, PPP_VERSION($PPP_VERSION),
	    [The real version of pppd represented as an int])
fi

AC_CHECK_HEADERS(resolv.h, dummy=yes,
	AC_MSG_ERROR(resolver header files are required))
AC_CHECK_LIB(resolv, ns_initparse, dummy=yes, [
	AC_CHECK_LIB(resolv, __ns_initparse, dummy=yes,
		AC_MSG_ERROR(resolver library support is required))
])

AC_CHECK_HEADERS([execinfo.h])
AM_CONDITIONAL([BACKTRACE], [test "${ac_cv_header_execinfo_h}" = "yes"])

AC_CHECK_MEMBERS([struct in6_pktinfo.ipi6_addr], [], [], [[#include <netinet/in.h>]])

AC_CHECK_FUNC(signalfd, dummy=yes,
			AC_MSG_ERROR(signalfd support is required))

AC_CHECK_LIB(dl, dlopen, dummy=yes,
			AC_MSG_ERROR(dynamic linking loader is required))

AC_ARG_ENABLE(iospm, AS_HELP_STRING([--enable-iospm],
		[enable Intel OSPM support]), [enable_iospm=${enableval}])
AM_CONDITIONAL(IOSPM, test "${enable_iospm}" = "yes")

AC_ARG_ENABLE(tist,
	AS_HELP_STRING([--enable-tist], [enable TI Shared Transport support]),
			[enable_tist=${enableval}], [enable_tist="no"])
AM_CONDITIONAL(TIST, test "${enable_tist}" != "no")
AM_CONDITIONAL(TIST_BUILTIN, test "${enable_tist}" = "builtin")

AC_ARG_ENABLE(session-policy-local,
	AS_HELP_STRING([--enable-session-policy-local], [enable local file Session policy configuration support]),
			[enable_session_policy_local=${enableval}], [enable_session_policy_local="no"])
AM_CONDITIONAL(SESSION_POLICY_LOCAL, test "${enable_session_policy_local}" != "no")
AM_CONDITIONAL(SESSION_POLICY_LOCAL_BUILTIN, test "${enable_session_policy_local}" = "builtin")

AC_ARG_WITH(stats-max-file-size, AS_HELP_STRING([--with-stats-max-file-size=SIZE],
			[Maximal size of a statistics round robin file]),
			[stats_max_file_size=${withval}])

if (test -z "${stats_max_file_size}"); then
   # default size is 16 kByte
   stats_max_file_size="16 * 8 * 128"
fi

AC_DEFINE_UNQUOTED([STATS_MAX_FILE_SIZE], (${stats_max_file_size}), [Maximal size of a statistics round robin file])

PKG_CHECK_MODULES(GIO, gio-2.0 >= 2.44, dummy=yes,
				AC_MSG_ERROR(GIo >= 2.44 is required))
AC_SUBST(GIO_CFLAGS)
AC_SUBST(GIO_LIBS)

PKG_CHECK_MODULES(GLIB, glib-2.0 >= 2.44, dummy=yes,
				AC_MSG_ERROR(GLib >= 2.44 is required))
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

PKG_CHECK_MODULES(DBUS, dbus-1 >= 1.4, dummy=yes,
				AC_MSG_ERROR(D-Bus >= 1.4 is required))
AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)

AC_ARG_WITH(dbusconfdir, AS_HELP_STRING([--with-dbusconfdir=PATH],
	[path to D-Bus config directory]), [path_dbusconf=${withval}],
		[path_dbusconf="`$PKG_CONFIG --variable=datadir dbus-1`"])
if (test -z "${path_dbusconf}"); then
	DBUS_CONFDIR="${datadir}/dbus-1/system.d"
else
	DBUS_CONFDIR="${path_dbusconf}/dbus-1/system.d"
fi
AC_SUBST(DBUS_CONFDIR)

AC_ARG_WITH(dbusdatadir, AS_HELP_STRING([--with-dbusdatadir=PATH],
	[path to D-Bus data directory]), [path_dbusdata=${withval}],
		[path_dbusdata="`$PKG_CONFIG --variable=datadir dbus-1`"])
if (test -z "${path_dbusdata}"); then
	DBUS_DATADIR="${datadir}/dbus-1/system-services"
else
	DBUS_DATADIR="${path_dbusdata}/dbus-1/system-services"
fi
AC_SUBST(DBUS_DATADIR)

AC_ARG_WITH([systemdunitdir], AS_HELP_STRING([--with-systemdunitdir=DIR],
	[path to systemd service directory]), [path_systemdunit=${withval}],
		[path_systemdunit="`$PKG_CONFIG --variable=systemdsystemunitdir systemd`"])
if (test -n "${path_systemdunit}"); then
	SYSTEMD_UNITDIR="${path_systemdunit}"
	AC_SUBST(SYSTEMD_UNITDIR)
fi
AM_CONDITIONAL(SYSTEMD, test -n "${path_systemdunit}")

AC_ARG_WITH([tmpfilesdir], AS_HELP_STRING([--with-tmpfilesdir=DIR],
	[path to systemd tmpfiles.d directory]), [path_tmpfiles=${withval}],
		[path_tmpfiles="`$PKG_CONFIG --variable=tmpfilesdir systemd`"])
if (test -n "${path_tmpfiles}"); then
	SYSTEMD_TMPFILESDIR="${path_tmpfiles}"
	AC_SUBST(SYSTEMD_TMPFILESDIR)
fi

AC_ARG_WITH(firewall, AS_HELP_STRING([--with-firewall=TYPE],
			[specify which firewall type is used iptables or nftables [default=iptables]]),
		[firewall_type=${withval}],
		[firewall_type="iptables"])

if (test "${firewall_type}" != "iptables" -a \
		"${firewall_type}" != "nftables"); then
	AC_MSG_ERROR(neither nftables nor iptables support enabled)
fi

found_iptables="no"
if (test "${firewall_type}" = "iptables"); then
	PKG_CHECK_MODULES(XTABLES, xtables >= 1.4.11, [found_iptables="yes"],
			AC_MSG_ERROR(Xtables library is required))
	AC_SUBST(XTABLES_CFLAGS)
	AC_SUBST(XTABLES_LIBS)
fi
AM_CONDITIONAL(XTABLES, test "${found_iptables}" != "no")

found_libmnl="no"
if (test "${firewall_type}" = "nftables" -o \
		"${enable_wireguard}" != "no"); then
	PKG_CHECK_MODULES(LIBMNL, [libmnl >= 1.0.0], [found_libmnl="yes"],
		AC_MSG_ERROR([libmnl >= 1.0.0 not found]))
	AC_SUBST(LIBMNL_CFLAGS)
	AC_SUBST(LIBMNL_LIBS)
fi
AM_CONDITIONAL(LIBMNL, test "${found_libmnl}" != "no")

found_nftables="no"
if (test "${firewall_type}" = "nftables"); then
	PKG_CHECK_MODULES(NFTABLES, [libnftnl >= 1.0.4], [found_nftables="yes"],
		AC_MSG_ERROR([libnftnl >= 1.0.4]))
	AC_SUBST(NFTABLES_CFLAGS)
	AC_SUBST(NFTABLES_LIBS)
fi
AM_CONDITIONAL(NFTABLES, test "${found_nftables}" != "no")

AC_ARG_ENABLE(test, AS_HELP_STRING([--enable-test],
		[enable test/example scripts]), [enable_test=${enableval}])
AM_CONDITIONAL(TEST, test "${enable_test}" = "yes")

AC_ARG_ENABLE(nmcompat, AS_HELP_STRING([--enable-nmcompat],
				[enable Network Manager support]),
			[enable_nmcompat=${enableval}], [enable_nmcompat="no"])
AM_CONDITIONAL(NMCOMPAT, test "${enable_nmcompat}" != "no")

AC_ARG_ENABLE(polkit, AS_HELP_STRING([--enable-polkit],
				[enable PolicyKit support]),
			[enable_polkit=${enableval}], [enable_polkit="no"])
if (test "${enable_polkit}" != "no"); then
	POLKIT_DATADIR="`$PKG_CONFIG --variable=actiondir polkit`"
	POLKIT_DATADIR=""
	if (test -z "${POLKIT_DATADIR}"); then
		POLKIT_DATADIR="${datadir}/polkit-1/actions"
	fi
	AC_SUBST(POLKIT_DATADIR)
fi
AM_CONDITIONAL(POLKIT, test "${enable_polkit}" != "no")

AC_ARG_ENABLE(selinux, AS_HELP_STRING([--enable-selinux],
				[enable selinux support]),
			[enable_selinux=${enableval}], [enable_selinux="no"])
AM_CONDITIONAL(SELINUX, test "${enable_selinux}" != "no")

AC_ARG_ENABLE(loopback, AS_HELP_STRING([--disable-loopback],
				[disable loopback support]),
					[enable_loopback=${enableval}])
AM_CONDITIONAL(LOOPBACK, test "${enable_loopback}" != "no")

AC_ARG_ENABLE(ethernet, AS_HELP_STRING([--disable-ethernet],
				[disable Ethernet support]),
					[enable_ethernet=${enableval}])
AM_CONDITIONAL(ETHERNET, test "${enable_ethernet}" != "no")

AC_ARG_ENABLE(wireguard, AS_HELP_STRING([--disable-wireguard],
				[disable Wireguard support]),
					[enable_wireguard=${enableval}])
AM_CONDITIONAL(WIREGUARD, test "${enable_wireguard}" != "no")
AM_CONDITIONAL(WIREGUARD_BUILTIN, test "${enable_wireguard}" = "builtin")

AC_ARG_ENABLE(gadget, AS_HELP_STRING([--disable-gadget],
				[disable USB Gadget support]),
					[enable_gadget=${enableval}])
AM_CONDITIONAL(GADGET, test "${enable_gadget}" != "no")

AC_ARG_ENABLE(wifi, AS_HELP_STRING([--disable-wifi],
				[disable WiFi support]),
					[enable_wifi=${enableval}])
AM_CONDITIONAL(WIFI, test "${enable_wifi}" != "no")

AC_ARG_ENABLE(iwd, AS_HELP_STRING([--enable-iwd],
				[enable iwd support]),
					[enable_iwd=${enableval}])
AM_CONDITIONAL(IWD, test "${enable_iwd}" = "yes")

AC_ARG_ENABLE(bluetooth, AS_HELP_STRING([--disable-bluetooth],
				[disable Bluetooth support]),
					[enable_bluetooth=${enableval}])
AM_CONDITIONAL(BLUETOOTH, test "${enable_bluetooth}" != "no")

AC_ARG_ENABLE(ofono, AS_HELP_STRING([--disable-ofono],
				[disable oFono support]),
					[enable_ofono=${enableval}])
AM_CONDITIONAL(OFONO, test "${enable_ofono}" != "no")

AC_ARG_ENABLE(dundee, AS_HELP_STRING([--disable-dundee],
				[disable dundee support (Bluetooth DUN)]),
					[enable_dundee=${enableval}])
AM_CONDITIONAL(DUNDEE, test "${enable_dundee}" != "no")

AC_ARG_ENABLE(pacrunner, AS_HELP_STRING([--disable-pacrunner],
				[disable PACrunner support]),
					[enable_pacrunner=${enableval}])
AM_CONDITIONAL(PACRUNNER, test "${enable_pacrunner}" != "no")

AC_ARG_ENABLE(neard, AS_HELP_STRING([--disable-neard],
				[disable Neard support]),
					[enable_neard=${enableval}])
AM_CONDITIONAL(NEARD, test "${enable_neard}" != "no")

AC_ARG_ENABLE(wispr, AS_HELP_STRING([--disable-wispr],
				[disable WISPr support]),
					[enable_wispr=${enableval}])
AM_CONDITIONAL(WISPR, test "${enable_wispr}" != "no")

AC_ARG_ENABLE(tools, AS_HELP_STRING([--disable-tools],
				[disable testing tools]),
					[enable_tools=${enableval}])
AM_CONDITIONAL(TOOLS, test "${enable_tools}" != "no")

AC_ARG_ENABLE(stats, AS_HELP_STRING([--disable-stats],
				[disable statistics round robin file generation]),
					[enable_stats=${enableval}])
AM_CONDITIONAL(STATS, test "${enable_stats}" != "no")

if (test "${enable_tools}" != "no"); then
	AC_PATH_PROGS(IPTABLES_SAVE, [iptables-save], [],
						$PATH:/sbin:/usr/sbin)
	AC_PATH_PROGS(IP6TABLES_SAVE, [ip6tables-save], [],
						$PATH:/sbin:/usr/sbin)
	IPTABLES_SAVE=$ac_cv_path_IPTABLES_SAVE
	IP6TABLES_SAVE=$ac_cv_path_IP6TABLES_SAVE
else
	IPTABLES_SAVE=""
	IP6TABLES_SAVE=""
fi
AC_SUBST(IPTABLES_SAVE)
AC_SUBST(IP6TABLES_SAVE)

AC_ARG_ENABLE(client, AS_HELP_STRING([--disable-client],
				[disable command line client]),
					[enable_client=${enableval}])
AM_CONDITIONAL(CLIENT, test "${enable_client}" != "no")

if (test "${enable_wispr}" != "no"); then
	PKG_CHECK_MODULES(GNUTLS, gnutls, dummy=yes,
				AC_MSG_ERROR(GnuTLS library is required))
else
	GNUTLS_CFLAGS=""
	GNUTLS_LIBS=""
fi
AC_SUBST(GNUTLS_CFLAGS)
AC_SUBST(GNUTLS_LIBS)

if (test "${enable_loopback}" != "no"); then
	AC_CHECK_HEADERS(sys/inotify.h, dummy=yes,
			AC_MSG_ERROR(inotify header files are required))

	AC_CHECK_LIB(c, inotify_init, dummy=yes,
			AC_MSG_ERROR(inotify library support is required))
fi

if (test "${enable_wifi}" != "no"); then
	AC_PATH_PROG(WPASUPPLICANT, [wpa_supplicant], [],
						$PATH:/sbin:/usr/sbin)
fi

AC_ARG_ENABLE(datafiles, AS_HELP_STRING([--disable-datafiles],
			[don't install configuration and data files]),
					[enable_datafiles=${enableval}])
AM_CONDITIONAL(DATAFILES, test "${enable_datafiles}" != "no")

if (test "${enable_client}" != "no"); then
	AC_CHECK_HEADERS(readline/readline.h, dummy=yes,
		AC_MSG_ERROR(readline header files are required))
fi

AM_CONDITIONAL(VPN, test "${enable_openconnect}" != "no" -o \
			"${enable_openvpn}" != "no" -o \
			"${enable_vpnc}" != "no" -o \
			"${enable_l2tp}" != "no" -o \
			"${enable_pptp}" != "no" -o \
			"${enable_wireguard}" != "no")

AC_MSG_CHECKING(which DNS backend to use)
AC_ARG_WITH(dns-backend, AS_HELP_STRING([--with-dns-backend=TYPE],
            [specify which DNS backend to use: internal or systemd-resolved [default=internal]]),
		[dns_backend=${withval}],
		[dns_backend="internal"])

if (test "${dns_backend}" != "internal" -a \
		"${dns_backend}" != "systemd-resolved"); then
	AC_MSG_ERROR(no suitable DNS backend defined)
fi
AM_CONDITIONAL(INTERNAL_DNS_BACKEND, test "${dns_backend}" = "internal")
AM_CONDITIONAL(SYSTEMD_RESOLVED_DNS_BACKEND, test "${dns_backend}" = "systemd-resolved")
AC_MSG_RESULT(${dns_backend})

AC_CONFIG_FILES(Makefile include/version.h connman.pc)
AC_OUTPUT

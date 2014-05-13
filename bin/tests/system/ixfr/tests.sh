#!/bin/sh
#
# Copyright (C) 2004, 2007, 2011, 2012, 2014  Internet Systems Consortium, Inc. ("ISC")
# Copyright (C) 2001  Internet Software Consortium.
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
# REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
# AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
# INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
# LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
# OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
# PERFORMANCE OF THIS SOFTWARE.

# $Id$

SYSTEMTESTTOP=..
. $SYSTEMTESTTOP/conf.sh

status=0

DIGOPTS="+tcp +noadd +nosea +nostat +noquest +nocomm +nocmd"
DIGCMD="$DIG $DIGOPTS @10.53.0.1 -p 5300"
SENDCMD="$PERL ../send.pl 10.53.0.2 5301"
RNDCCMD="$RNDC -s 10.53.0.1 -p 9953 -c ../common/rndc.conf"

echo "I:testing initial AXFR"

$SENDCMD <<EOF
/SOA/
nil.      	300	SOA	ns.nil. root.nil. 1 300 300 604800 300
/AXFR/
nil.      	300	SOA	ns.nil. root.nil. 1 300 300 604800 300
/AXFR/
nil.      	300	NS	ns.nil.
nil.		300	TXT	"initial AXFR"
a.nil.		60	A	10.0.0.61
b.nil.		60	A	10.0.0.62
/AXFR/
nil.      	300	SOA	ns.nil. root.nil. 1 300 300 604800 300
EOF

sleep 1

# Initially, ns1 is not authoritative for anything (see setup.sh).
# Now that ans is up and running with the right data, we make it
# a slave for nil.

cat <<EOF >>ns1/named.conf
zone "nil" {
	type slave;
	file "myftp.db";
	masters { 10.53.0.2; };
};
EOF

$RNDCCMD reload

for i in 0 1 2 3 4 5 6 7 8 9
do
	$DIGCMD nil. SOA > dig.out
	grep "SOA" dig.out > /dev/null && break
	sleep 1
done

$DIGCMD nil. TXT | grep 'initial AXFR' >/dev/null || {
    echo "I:failed"
    status=1
}

echo "I:testing successful IXFR"

# We change the IP address of a.nil., and the TXT record at the apex.
# Then we do a SOA-only update.

$SENDCMD <<EOF
/SOA/
nil.      	300	SOA	ns.nil. root.nil. 3 300 300 604800 300
/IXFR/
nil.      	300	SOA	ns.nil. root.nil. 3 300 300 604800 300
nil.      	300	SOA	ns.nil. root.nil. 1 300 300 604800 300
a.nil.      	60	A	10.0.0.61
nil.		300	TXT	"initial AXFR"
nil.      	300	SOA	ns.nil. root.nil. 2 300 300 604800 300
nil.		300	TXT	"successful IXFR"
a.nil.      	60	A	10.0.1.61
nil.      	300	SOA	ns.nil. root.nil. 2 300 300 604800 300
nil.      	300	SOA	ns.nil. root.nil. 3 300 300 604800 300
nil.      	300	SOA	ns.nil. root.nil. 3 300 300 604800 300
EOF

sleep 1

$RNDCCMD refresh nil

sleep 2

$DIGCMD nil. TXT | grep 'successful IXFR' >/dev/null || {
    echo "I:failed"
    status=1
}

echo "I:testing AXFR fallback after IXFR failure"

# Provide a broken IXFR response and a working fallback AXFR response

$SENDCMD <<EOF
/SOA/
nil.      	300	SOA	ns.nil. root.nil. 4 300 300 604800 300
/IXFR/
nil.      	300	SOA	ns.nil. root.nil. 4 300 300 604800 300
nil.      	300	SOA	ns.nil. root.nil. 3 300 300 604800 300
nil.      	300	TXT	"delete-nonexistent-txt-record"
nil.      	300	SOA	ns.nil. root.nil. 4 300 300 604800 300
nil.      	300	TXT	"this-txt-record-would-be-added"
nil.      	300	SOA	ns.nil. root.nil. 4 300 300 604800 300
/AXFR/
nil.      	300	SOA	ns.nil. root.nil. 3 300 300 604800 300
/AXFR/
nil.      	300	NS	ns.nil.
nil.      	300	TXT	"fallback AXFR"
/AXFR/
nil.      	300	SOA	ns.nil. root.nil. 3 300 300 604800 300
EOF

sleep 1

$RNDCCMD refresh nil

sleep 2

$DIGCMD nil. TXT | grep 'fallback AXFR' >/dev/null || {
    echo "I:failed"
    status=1
}

echo "I:testing DiG's handling of a multi message AXFR style IXFR response" 
(
(sleep 10 && kill $$) 2>/dev/null &
sub=$!
$DIG ixfr=0 large -p 5300 @10.53.0.3 > dig.out
kill $sub
)
lines=`grep hostmaster.large dig.out | wc -l`
test ${lines:-0} -eq 2 || { echo "I:failed"; status=1; }
messages=`sed -n 's/^;;.*messages \([0-9]*\),.*/\1/p' dig.out`
test ${messages:-0} -gt 1 || { echo "I:failed"; status=1; }

echo "I:test 'dig +notcp ixfr=<value>' vs 'dig ixfr=<value> +notcp' vs 'dig ixfr=<value>'"
ret=0
# Should be "switch to TCP" response
$DIG +notcp ixfr=1 test -p 5300 @10.53.0.4 > dig.out1 || ret=1
$DIG ixfr=1 +notcp test -p 5300 @10.53.0.4 > dig.out2 || ret=1
$PERL ../digcomp.pl dig.out1 dig.out2 || ret=1
awk '$4 == "SOA" { soacnt++} END {exit(soacnt == 1 ? 0 : 1);}' dig.out1 || ret=1
awk '$4 == "SOA" { exit($7 == 4 ? 0 : 1);}' dig.out1 || ret=1
# Should be incremental transfer.
$DIG ixfr=1 test -p 5300 @10.53.0.4 > dig.out3 || ret=1
awk '$4 == "SOA" { soacnt++} END {exit(soacnt == 6 ? 0 : 1);}' dig.out3 || ret=1
if [ ${ret} != 0 ]; then
	echo "I:failed";
	status=1;
fi

echo "I:exit status: $status"
exit $status

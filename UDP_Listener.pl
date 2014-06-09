#!/usr/bin/perl -w
# UDP xAP listener
use strict;
use IO::Socket;
my($msglen,$sock, $newmsg, $MAXLEN, $PORTNO);
my($lasttimestamp,$timestamp,$tempr,$watts,$errcount,$timeouts);

$MAXLEN = 1024;
#$PORTNO = 3639;		# Default xAP Port
$PORTNO = 3865;		# Default xPL Port

$sock = IO::Socket::INET->new(LocalPort => $PORTNO,
        Proto => 'udp',
        LocalAddr => '0.0.0.0',
        Broadcast => 1)
    or die "socket: $@";
print "Awaiting UDP messages on port $PORTNO\n";
$lasttimestamp = 0;
while ($sock->recv($newmsg, $MAXLEN)) {
    $msglen = length($newmsg);
	print "-" x 70, "\n", scalar localtime, "\n", $newmsg;
}
die "recv: $!";

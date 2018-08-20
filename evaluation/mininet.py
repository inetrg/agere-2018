#!/usr/bin/python

mininet_path = '/usr/local/lib/python2.7/dist-packages/mininet-2.2.2-py2.7.egg'

import time
import sys

sys.path.insert(0, mininet_path)

from mininet.topo import Topo
from mininet.net import Mininet
from mininet.link import TCLink
from mininet.cli import CLI
from mininet.node import CPULimitedHost
from mininet.util import pmonitor

class TwoHostsTopology(Topo):
    "peer to peer topology"

    def build(self, ls=0, dy='10ms'):
        "Build two nodes with a direct link"
        print('loss = %d, delay = %s' % (ls, dy))

        # Add two hosts.
        lhs = self.addHost('h1', cpu=.5)
        rhs = self.addHost('h2', cpu=.5)

        # Add direct link.
        self.addLink(lhs, rhs)
        # if ls != 0:
            # linkopts = dict(bw=100, delay=dy, loss=ls) #, use_htb=True)
            # self.addLink(lhs, rhs, **linkopts)
        # else:
            # linkopts = dict(bw=100, delay=dy) #, use_htb=True)
            # self.addLink(lhs, rhs, **linkopts)

# topos = { 'p2p': ( lambda: TwoHostsTopology() ) }

def set_interface_delay(host, action, interface, delay): #action: add/change/delete
    # command = 'tc qdisc {} dev {} root handle 1: netem delay {}ms'.format(action, interface, delay)
    variance = delay / 10
    distribution = 'normal'
    command = 'tc qdisc {} dev {} root netem delay {}ms {}ms distribution {}'.format(action, interface, delay, variance, distribution)
    print('running "{}"'.format(command))
    print host.cmd(command)

def set_interface_packet_lost(host, action, interface, packet_loss): #action: add/change/delete
    # command = 'tc qdisc {} dev {} parent 1:1 netem loss {}%'.format(action, interface, packet_loss)
    command = 'tc qdisc {} dev {} root netem loss {}%'.format(action, interface, packet_loss)
    print('running: "{}"').format(command)
    print host.cmd(command)

def set_loss(host, packet_loss):
    iface = '{}-eth0'.format(host.name)
    op = 'add' if packet_loss > 0 else 'del'
    print('setting loss on "{}" to "{}%" using "{}"'.format(iface, packet_loss, op))
    set_interface_packet_lost(host, op, iface, packet_loss)

def set_delay(host, delay):
    iface = '{}-eth0'.format(host.name)
    op = 'add' if delay > 0 else 'del'
    print('setting delay on "{}" to "{}ms" using "{}"'.format(iface, delay, op))
    set_interface_delay(host, op, iface, delay)

def get_info(host):
    iface = '{}-eth0'.format(host.name)
    command = 'tc qdisc show  dev {}'.format(iface)
    print('running: "{}"'.format(command))
    host.cmdPrint(command)

def main():
    delay = 10
    for loss in range(11):
        for run in range(10):
            print("creating topology with loss {}%".format(loss))
            # host=CPULimitedHost,
            net = Mininet(topo = TwoHostsTopology(ls=loss), link=TCLink, host=CPULimitedHost)
            net.start()

            print("configuring hosts")
            h1 = net.get('h1')
            set_delay(h1, delay)
            set_loss(h1, loss)
            # get_info(h1) # does not print anything ...
            h2 = net.get('h2')
            set_delay(h2, delay)
            set_loss(h2, loss)
            # get_info(h2)

            files = {}
            file_path = '/home/localadmin/logs' # was /tmp
            servererr = open('{}/ppserver-{}-{}.err'.format(file_path, loss, run), 'w')
            serverout = open('{}/ppserver-{}-{}.out'.format(file_path, loss, run), 'w')
            clienterr = open('{}/ppclient-{}-{}.err'.format(file_path, loss, run), 'w')
            clientout = open('{}/ppclient-{}-{}.out'.format(file_path, loss, run), 'w')

            print("starting server")
            #servercommand = '../build/bin/pingpong -s --host=\\"{}\\" >/tmp/ppserver-{}.out 2>/tmp/ppserver-{}.err'.format(h1.IP(), loss, loss)
            servercommand = '../build/bin/pingpong -s --host=\\"{}\\"'.format(h1.IP())
            print(servercommand)
            p1 = h1.popen(servercommand, shell=True, universal_newlines=True, stdout=serverout, stderr=servererr) # not sure if this shouldn't be h1.cmd

            print("starting client")
            #clientcommand = '../build/bin/pingpong -m 2000 --host=\\"{}\\" >/tmp/ppclient-{}.out 2>/tmp/ppclient-{}.err'.format(h1.IP(), loss, loss)
            clientcommand = '../build/bin/pingpong -m 2000 --host=\\"{}\\"'.format(h1.IP())
            print(clientcommand)
            p2 = h2.popen(clientcommand, shell=True, universal_newlines=True, stdout=clientout, stderr=clienterr) # not sure if this shouldn't be h1.cmd

            # for host, line in pmonitor( popens ):
                # if host:
                    # print("<%s>: %s" % ( host.name, line ))

            # Wait for p2 to finish
            # CLI(net)
            # p1.wait()
            # p1.terminate()
            p2.wait()
            p1.kill()
            # p2.terminate()
            # p1.terminate()
            net.stop()
            servererr.close()
            serverout.close()
            clienterr.close()
            clientout.close()
            h1.cmd('killall pingpong')
            h2.cmd('killall pingpong')

if __name__ == '__main__':
    main()


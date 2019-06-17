#!/usr/bin/python

#mininet_path = '/usr/local/lib/python2.7/dist-packages/mininet-2.2.2-py2.7.egg'
#mininet_path = '/usr/local/lib/python2.7/dist-packages/mininet-2.3.0d4-py2.7.egg'

import time
import sys
import argparse
import mininet

from mininet.topo import Topo
from mininet.net import Mininet
from mininet.link import TCLink
from mininet.cli import CLI
from mininet.node import CPULimitedHost
from mininet.util import pmonitor
from time import time

from time import sleep

class TwoHostsTopology(Topo):
    "The peer to peer topology for our benchmark."

    def build(self, n=2):
        "Build two nodes with a direct link"

        # Add two hosts.
        lhs = self.addHost('h1', cpu=.5)
        rhs = self.addHost('h2', cpu=.5)

        # Add direct link.
        self.addLink(lhs, rhs)

topos = { 'p2p': ( lambda: TwoHostsTopology() ) }

def set_interface_delay(host, action, interface, delay): #
    "Helper fun for set loss on a specific interface, action can be: add/change/delete"

    # command = 'tc qdisc {} dev {} root handle 1: netem delay {}ms'.format(action, interface, delay)
    command = 'tc qdisc {} dev {} root netem delay {}ms'.format(action, interface, delay)
    #command = ''
    #if (delay > 0):
    #    variance = delay / 10
    #    distribution = 'normal'
    #    command = 'tc qdisc {} dev {} root netem delay {}ms {}ms distribution {}'.format(action, interface, delay, variance, distribution)
    #else:
    #    command = 'tc qdisc {} dev {} root netem delay {}ms'.format(action, interface, delay)
    print('{} -> "{}"'.format(host.name, command))
    host.cmdPrint(command)

def set_interface_packet_lost(host, action, interface, packet_loss): #action: add/change/delete
    "Helper fun to set loss on a sepcific interface."

    # command = 'tc qdisc {} dev {} parent 1:1 netem loss {}%'.format(action, interface, packet_loss)
    command = 'tc qdisc {} dev {} root netem loss {}%'.format(action, interface, packet_loss)
    print('{} -> "{}"').format(host.name, command)
    host.cmd(command)

def set_loss(host, packet_loss):
    "Set loss for eth0 on a given host."
    iface = '{}-eth0'.format(host.name)
    op = 'add' if packet_loss > 0 else 'del'
    set_interface_packet_lost(host, op, iface, packet_loss)

def set_delay(host, delay):
    "set delay for eth0 on a given host."
    iface = '{}-eth0'.format(host.name)
    op = 'add' if delay > 0 else 'del'
    set_interface_delay(host, op, iface, delay)

def conf_host(host, delay, loss):
    iface = '{}-eth0'.format(host.name)
    if delay == 0 or loss == 0:
        command = 'tc qdisc del dev {} root netem'.format(iface)
        print('{} -> "{}"'.format(host.name, command))
    command = 'tc qdisc add dev {} root netem'.format(iface)
    if delay == 0 and loss == 0:
        return
    if loss > 0:
        command = '{} loss {}%'.format(command, loss)
    if delay > 0:
        command = '{} delay {}ms'.format(command, delay)
    print('{} -> "{}"'.format(host.name, command))
    host.cmd(command)

def get_info(host):
    "Print tc info."
    iface = '{}-eth0'.format(host.name)
    command = 'tc qdisc show dev {} >> stats.log'.format(iface)
    print('{} -> "{}"'.format(host.name, command))
    host.cmdPrint(command)

def set_min_rto(host, timeout):
    "Change min_rto for a route on Linux, might require the 'ms' suffix."
    command = 'ip route change 10.0.0.0/8 dev {}-eth0 proto kernel scope link src {} rto_min {}'.format(host.name, host.IP(), timeout)
    print('{} -> "{}"'.format(host.name, command))
    host.cmdPrint(command)

def main():
    parser = argparse.ArgumentParser(description='CAF newbs on Mininet.')
    parser.add_argument('-l', '--loss',    help='set packet loss in percent (0)', type=int, default=0)
    parser.add_argument('-d', '--delay',   help='set link delay in ms       (0)', type=int, default=0)
    parser.add_argument('-r', '--runs',    help='set number of runs         (1)', type=int, default=1)
    parser.add_argument('-T', '--threads', help='set number of threads      (1)', type=int, default=1)
    parser.add_argument('-R', '--rto',     help='set min rto for TCP       (40)', type=int, default=40)
    parser.add_argument('-o', '--ordered', help='enable ordering for UDP       ', action='store_true')
    parser.add_argument('-b', '--big-file', help='send big files instead of pingpong', action='store_true')
    parser.add_argument('-m', '--mode', help='set mode [1M|10M|100M|1G]', type=string, default='1M')
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('-t', '--tcp',  help='use TCP' , action='store_true')
    group.add_argument('-u', '--udp',  help='use UDP' , action='store_true')
    group.add_argument('-q', '--quic', help='use QUIC', action='store_true')
    
    args = vars(parser.parse_args())
    run = 0
    max_runs = args['runs']
    while run < max_runs:
        is_tcp = args['tcp']
        loss = args['loss']
        delay = args['delay']
        min_rto = args['rto']
        proto = ''
        if args['tcp']:
            proto = 'tcp'
        elif args['udp']:
            if args['ordered']:
                proto = 'udp-ordered'
            else:
                proto = 'udp'
        elif args['quic']:
            proto = 'quic'
        print(">> Run {} with {}% loss and {}ms delay".format(run, loss, delay))
        net = Mininet(topo = TwoHostsTopology(), link=TCLink, host=CPULimitedHost)
        net.start()

        print("Configuring hosts ...")
        # host 1
        h1 = net.get('h1')
        conf_host(h1, delay, loss)
        # set_delay(h1, delay)
        # set_loss(h1, loss)
        if is_tcp:
            set_min_rto(h1, min_rto)
        get_info(h1)
        # host 2
        h2 = net.get('h2')
        # set_delay(h2, delay)
        # set_loss(h2, loss)
        conf_host(h2, delay, loss)
        if is_tcp:
            set_min_rto(h2, min_rto)
        get_info(h2)

        servererr = open('./pingpong/{}-server-{}-{}-{}.err'.format(proto, loss, delay, run), 'w')
        serverout = open('./pingpong/{}-server-{}-{}-{}.out'.format(proto, loss, delay, run), 'w')
        clienterr = open('./pingpong/{}-client-{}-{}-{}.err'.format(proto, loss, delay, run), 'w')
        clientout = open('./pingpong/{}-client-{}-{}-{}.out'.format(proto, loss, delay, run), 'w')

        caf_opts = '--scheduler.max-threads={}'.format(args['threads'])
        prog = ''
        if args['tcp']:
            if args['big-file']:
                prog = 'tcp_big_data'
                if args['mode']:
                    caf_opts = '{} -b {}'.format(caf_opts, args['mode'])
            else:
                prog = 'pingpong_tcp'
        elif args['udp']:
            if args['big-file']:
                prog = 'udp_big_data'
                if args['mode']:
                    caf_opts = '{} -b {}'.format(caf_opts, args['mode'])
            else:
                prog = 'pingpong_udp'
            if args['ordered']:
                caf_opts = '{} --ordered'.format(caf_opts)
        elif args['quic']:
            if args['big-file']:
                prog = 'quic_big_data'
                if args['mode']:
                    caf_opts = '{} -b {}'.format(caf_opts, args['mode'])
            else:
                prog = 'pingpong_quic'
        print("Starting server")
        servercommand = '../build/bin/{} -s {} '.format(prog, caf_opts)
        print('> {}'.format(servercommand))
        # h1.cmdPrint(servercommand)
        # p1 = int(h1.cmd('echo $!'))
        p1 = h1.popen(servercommand, shell=True, universal_newlines=True, stdout=serverout, stderr=servererr)

        # Give everything a bit of time to start.
        sleep(1)

        print("Starting client")
	      # previous --host=\\"{}\\" ...
        clientcommand = '../build/bin/{} -m 2000 --host={} {}'.format(prog, h1.IP(), caf_opts)
        print('> {}'.format(clientcommand))
        # h2.cmdPrint(clientcommand)
        # p2 = int(h2.cmd('echo $!'))
        p2 = h2.popen(clientcommand, shell=True, universal_newlines=True, stdout=clientout, stderr=clienterr)

        # CLI(net)
        p2.wait()
        p1.kill()
        servererr.close()
        serverout.close()
        clienterr.close()
        clientout.close()
        h1.cmd('kill %{}'.format(prog))
        h1.cmd('killall {}'.format(prog))
        h2.cmd('kill %{}'.format(prog))
        h2.cmd('killall {}'.format(prog))
        # sleep(1)
        net.stop()
        # sleep(1)
        run += 1

if __name__ == '__main__':
    main()


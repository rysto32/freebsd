#!/usr/bin/env python

import sys

f = open(sys.argv[1], 'r')

if (sys.argv[1] == 'options'):
	config_node = 'kern-options'
else:
	config_node = 'kern-arch-options'

print("{} = [".format(config_node))
for line in f:
	words = line.split()
	if not words or words[0].startswith('#'):
		continue

	opt = words[0]
	if len(words) == 1 or words[1].startswith('#'):
		file = ''
	else:
		file = words[1]

	print('\t{')
	print('\t\toption = "{}",'.format(opt))
	if file:
		print('\t\theader = "{}",'.format(file))
	print('\t},')

print(']')


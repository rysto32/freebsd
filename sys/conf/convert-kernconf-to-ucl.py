#!/usr/bin/env python

import sys

f = open(sys.argv[1], 'r')

keys={}
if sys.argv[1] != "DEFAULTS":
	keys['include'] = [['DEFAULTS']]
order=[]

for line in f:
	words = line.split()
	if not words or words[0].startswith('#'):
		continue

	if words[0] == 'nodevice' or words[0] == 'nooptions':
		k = 'options'
		value=[words[1], 'false']
	else:
		if words[0] == 'device' or words[0] == 'cpu':
			k = 'options'
		else:
			k = words[0]
		value = words[1].split('=', 2)

	if k in keys:
		keys[k].append(value)
	else:
		keys[k] = [value]
		order.append(k)

if 'include' in keys:
	for path in keys['include']:
		print(".include {}.ucl".format(path[0]))

print("kernconf = {")
for k in order:
	if k == 'include':
		continue

	if len(keys[k]) == 1:
		value = keys[k][0]
		if len(value) == 1:
			print('\t{} = {},'.format(k, value[0]))
		else:
			print('\t{} = {{{} = {}}},'.format(k, value[0], value[1]))
	else:
		print('\t{} = ['.format(k))
		for value in keys[k]:
			if len(value) == 1:
				print('\t\t{},'.format(value[0]))
			else:
				print('\t\t{{{} = {}}},'.format(value[0], value[1]))
		print('\t]')

print('}')

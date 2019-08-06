#!/usr/bin/env python

import sys

BEGIN_STATE = 1
STRING_STATE = 2
DONE_STATE = 3

class Lexer:
	state = BEGIN_STATE
	current_word = ''
	words = []

	def __init__(self, f):
		self.file = f

	def next_words(self):
		if (self.state == BEGIN_STATE):
			return self.next_begin_state()
		elif (self.state == STRING_STATE):
			return self.next_string_state()
		elif self.state == DONE_STATE:
			return []

	def next_begin_state(self):
		while self.file:
			ch = self.file.read(1)

			if not ch:
				break
			elif ch == '"' or ch == "'":
				self.finish_word()
				self.current_word += ch
				self.state = STRING_STATE
				self.close_str = ch
				return self.next_words()
			elif ch == '#':
				# Discard comment contents
				self.file.readline()
				return self.flush_words()
			elif ch == '\\':
				next = self.file.read(1)
				if next != '\n':
					print("Not newline '{}' escaped".format(next), file=sys.stderr)
					sys.exit(1)
			elif ch == '\n':
				return self.flush_words()
			elif ch.isspace():
				self.finish_word()
			else:
				self.current_word += ch
		self.state = DONE_STATE
		return self.flush_words()

	def next_string_state(self):
		while self.file:
			ch = self.file.read(1)
			self.current_word += ch

			if not ch:
				break
			elif ch == self.close_str:
				self.finish_word()
				self.state = BEGIN_STATE
				return self.next_words()
		self.state = DONE_STATE
		return self.flush_words()


	def finish_word(self):
		if self.current_word:
			self.words.append(self.current_word)
			self.current_word = ''

	def flush_words(self):
		self.finish_word()
		w = self.words
		self.words = []
		return w

	def has_data(self):
		return self.state != DONE_STATE

def HandleFlag(keys, name, words, pos):
	keys[name] = True
	return pos

def HandleArg(keys, name, words, pos):
	keys[name] = words[pos]
	return pos + 1

def StringPrinter(name, value):
	if value.startswith("'") or value.startswith('"'):
		print('\t\t\t{} : {}'.format(name, value))
	else:
		print('\t\t\t{} : "{}"'.format(name, value))

def FlagPrinter(name, value):
	print('\t\t\t{} : {}'.format(name, 'true' if value else 'false'))

def OptionPrinter(name, value):
	if len(value) == 1:
		if len(value[0]) == 1:
			print('\t\t\t{} : "{}"'.format(name, value[0][0]))
		else:
			comma = ''
			print('\t\t\t{} : {{ all-of : ['.format(name), end='')
			for v in value[0]:
				print('{}"{}"'.format(comma, v), end='')
				comma = ', '
			print(']}')
	else:
		outercomma=''
		print('\t\t\t{} : {{any-of : ['.format(name), end='')
		for optlist in value:
			if len(optlist) == 1:
				print('{}"{}"'.format(outercomma, optlist[0]), end='')
				outercomma=', '
			else:
				innercomma = ''
				print('{}{{ all-of : ['.format(outercomma), end='')
				for opt in optlist:
					print('{}"{}"'.format(innercomma, opt), end='')
					innercomma=', '
				print(']}', end='')
				outercomma = ', '
		print(']}')


if len(sys.argv) != 2:
	print("usage: convert-files.py <file>")
	sys.exit(1)

f = open(sys.argv[1], 'r')
lex = Lexer(f)

if (sys.argv[1] == 'files'):
	config_node = 'kern-src'
else:
	config_node = 'kern-arch-src'

keys = {}
keywords = {
	#'profiling-routine' : HandleFlag,
	'no-obj' : HandleFlag,
	'compile-with' : HandleArg,
	'no-implicit-rule' : HandleFlag,
	'dependency' : HandleArg,
	'before-depend' : HandleFlag,
	'local' : HandleFlag,
	'clean' : HandleArg,
	'warning' : HandleArg,
	'obj-prefix' : HandleArg
}

# Use a list to ensure that keys are printed in a consistent order
order = [
	{ 'key' : 'path', 'printer' : StringPrinter },
	{ 'key' : 'required', 'printer' :  FlagPrinter},
	{ 'key' : 'options', 'printer' :  OptionPrinter},
	{ 'key' : 'no-obj', 'printer' :  FlagPrinter},
	{ 'key' : 'compile-with', 'printer' :  StringPrinter},
	{ 'key' : 'no-implicit-rule', 'printer' :  FlagPrinter},
	{ 'key' : 'dependency', 'printer' :  StringPrinter},
	{ 'key' : 'before-depend', 'printer' :  FlagPrinter},
	{ 'key' : 'local', 'printer' :  FlagPrinter},
	{ 'key' : 'clean', 'printer' :  StringPrinter},
	{ 'key' : 'warning', 'printer' :  StringPrinter},
	{ 'key' : 'obj-prefix', 'printer' :  StringPrinter},
]

print('{')
print('\t{} = ['.format(config_node))
while lex.has_data():
	words = lex.next_words()

	if not words:
		continue

	keys = {}
	keys['path'] = words[0]

	if words[1] == 'standard':
		keys['standard'] = True
		pos = 2
	elif words[1] == 'optional':
		options = []
		optlist = []

		# Have to use a while loop here to ensure that i == len(words)
		# if this loop consumes all value
		i = 2
		while i < len(words):
			opt = words[i]
			if (opt == '|'):
				options.append(optlist)
				optlist = []
			elif opt in keywords:
				break
			else:
				optlist.append(opt)
			i = i + 1

		pos = i
		options.append(optlist)
		keys['options'] = options
	else:
		print("Unexpected keyword '{}'".format(words[1]), file=sys.stderr)
		sys.exit(1)

	i = pos
	while i < len(words):
		optname = words[i]
		i = keywords[optname](keys, optname, words, i + 1)

	print('\t\t{')
	for o in order:
		key = o['key']
		if (key in keys):
			o['printer'](key, keys[key])
	print('\t\t},')
	#if 'required' in keys:
		#print('{} required'.format(keys['name']))
	#else:
		#print('{} options: {}'.format(keys['name'], keys['options']))
	words=[]

print('\t]\n}')



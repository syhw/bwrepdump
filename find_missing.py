import sys, os, shutil

# usage: python find_missing.py NEED_DIR HAVE_DIR optionalCOPY?
# if you specify a third argument (optionalCOPY?), you need to have the 
# "todo" directory existing at the same place of this script

need = set()
have = set()
for fname in os.listdir(sys.argv[1]):
	need.add(fname.split('.')[0])
for fname in os.listdir(sys.argv[2]):
	have.add(fname.split('.')[0])

for e in need:
	if e not in have:
		print e
		if len(sys.argv) > 3:
			shutil.copyfile(sys.argv[1]+e+".rep", "todo/"+e+".rep")

	


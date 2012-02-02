import sys, os, shutil

# usage: python cp.py FROM_DIR PREFIX
# use the "MkDir.sh" script to make the needed directories

for dname in os.listdir(sys.argv[1]):
	if os.path.isdir(sys.argv[1]+dname):
		print dname
		for fname in os.listdir(sys.argv[1]+dname):
			if sys.argv[2] in fname:
				print fname
				shutil.copyfile(sys.argv[1]+dname+'/'+fname, "extract/"+dname+'/'+fname)

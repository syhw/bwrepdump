import sys, os, shutil

for dname in os.listdir(sys.argv[1]):
	if os.path.isdir(sys.argv[1]+dname):
		print dname
		for fname in os.listdir(sys.argv[1]+dname):
			if sys.argv[2] in fname:
				print fname
				shutil.copyfile(sys.argv[1]+dname+'/'+fname, "extract/"+dname+'/'+fname)

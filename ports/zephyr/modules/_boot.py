import zephyr, sys

def init_vfs():
	if zephyr.BlockDevicesID:
		if "Flash" in zephyr.BlockDevicesID:
			import vfs, os
			fs = None
			block_dev = None
			try:
				block_dev = zephyr.FlashArea(zephyr.BlockDevicesID["Flash"], 4096)
				fs = vfs.VfsLfs2(block_dev)
				vfs.mount(fs, "/flash")
				os.chdir("/flash")
				sys.path.append("/flash/lib")
			except:
				yesno = "a"
				while not (yesno[0] in ["y","n"]):
					yesno = input("Couldn't mount flash storage FS, format?(y/n)")
				if yesno[0] == "y":
					try:
						block_dev = zephyr.FlashArea(zephyr.BlockDevicesID["Flash"], 4096)
						vfs.VfsLfs2.mkfs(block_dev)
						fs = vfs.VfsLfs2(block_dev)
						vfs.mount(fs, "/flash")
						os.chdir("/flash")
						os.mkdir('lib')
						sys.path.append("/flash/lib")
					except:
						print("Failed Formatting")

init_vfs()

del init_vfs

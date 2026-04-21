#include <linux/module.h>
#include <linux/export-internal.h>
#include <linux/compiler.h>

MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};


MODULE_INFO(depends, "gspca_main");

MODULE_ALIAS("usb:v1871p01B0d*dc*dsc*dp*ic*isc*ip*in*");

MODULE_INFO(srcversion, "EA7954B50DC7B0FBBDA68C7");

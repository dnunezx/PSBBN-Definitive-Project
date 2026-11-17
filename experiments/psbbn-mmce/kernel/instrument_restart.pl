use strict;
use warnings;
my $path = "$ARGV[0]/arch/mips/ps2/reset.c";
open(my $in, '<', $path) or die $!;
local $/; my $text = <$in>; close($in);
die 'Restart trace already present' if index($text, 'PSBBN launch:') >= 0;
my $n = ($text =~ s/void ps2_machine_restart\(char \*command\)\n\{\n/void ps2_machine_restart(char *command)\n{\n\tprintk(KERN_NOTICE "PSBBN launch: restart entered\\n");\n/);
die 'Restart entry missing' unless $n == 1;
$n = ($text =~ s/\tsys_sync\(\);\n\tps2_exit_drivers\(\);\n#ifdef CONFIG_AKMEM/\tsys_sync();\n\tprintk(KERN_NOTICE "PSBBN launch: filesystem sync done\\n");\n\tps2_exit_drivers();\n\tprintk(KERN_NOTICE "PSBBN launch: drivers stopped, entering AKMEM\\n");\n#ifdef CONFIG_AKMEM/);
die 'Restart shutdown sequence missing' unless $n == 1;
for my $name (qw(ps2mcfs_cleanup ps2pad_cleanup ps2cdvd_cleanup ps2sd_cleanup ps2mc_cleanup ps2rm_cleanup ps2sysconf_cleanup smap_cleanup_module ps2dev_cleanup)) {
    my $old = "\t$name();";
    my $at = index($text, $old);
    die "Missing $name" if $at < 0;
    substr($text, $at, length($old), "\tprintk(KERN_NOTICE \"PSBBN launch: $name\\n\");\n$old");
}
open(my $out, '>', $path) or die $!; print $out $text; close($out);

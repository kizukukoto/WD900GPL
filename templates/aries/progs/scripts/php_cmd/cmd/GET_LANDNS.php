<?
include "/htdocs/phplib/trace.php";
include "/htdocs/phplib/xnode.php";
$i=0;
TRACE_debug("COUNT=".$COUNT);
TRACE_debug("PREFIX=".$PREFIX);
while($i<$COUNT)
{
	$VALUE="VALUE".$i;
	TRACE_debug("VALUE".$i."=".$$VALUE);
	$i++;
}
$path_lan=XNODE_getpathbytarget($PREFIX."/module", "inf", "uid","LAN-1", 0);
if($COUNT==0)
{
	$lan_dns=query($path_lan."/dns4");
	if ($lan_dns=="DNS4-1")
	{
		echo "LAN DNS  ENABLED\n";
	}
	else
	{
		echo "LAN DNS DISABLED\n";
	}
	set($PREFIX."/statue","0");
}
else
{
	/*invalid parameter*/	
	set($PREFIX."/statue","7");
}

?>
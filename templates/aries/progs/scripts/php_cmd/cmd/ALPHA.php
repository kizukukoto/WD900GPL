<?
include "/htdocs/phplib/trace.php";
include "/htdocs/phplib/xnode.php";
TRACE_debug("COUNT=".$COUNT);
TRACE_debug("PREFIX=".$PREFIX);
if($COUNT==1)
{
	set($PREFIX."/statue","0");
	set("/runtime/session/function","4");
}
else
{
	/*invalid parameter*/	
	set($PREFIX."/statue","7");
}
?>
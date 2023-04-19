#include <ansi.h>

inherit STD_ITEM;

private void create()
{
    set_name("一千零一夜", ({"book"}));
    set("unit", "本");
    set("long", @LONG
这是一本民间文学小说，没事可以读读（read）。
    该作讲述古代有一个萨桑国，国王山鲁亚尔生性残暴嫉妒，因王后行为不端，
将其杀死，此后每日娶一少女，翌日晨即杀掉，以示报复。宰相的女儿山鲁佐德为
拯救无辜的女子，自愿嫁给国王，用讲述故事方法吸引国王，每夜讲到最精彩处，
天刚好亮了，使国王爱不忍杀，允她下一夜继续讲。她的故事一直讲了一千零一夜，
国王终于被感动，与她白首偕老。
LONG
    );
}

void init()
{
    // write(RED "[" + this_object() + "]->init():" + this_player() + "\n" NOR);
    add_action("do_read", "read");
}

int do_read(string arg)
{
    string *capter;
    capter = ({
        "第01部：国王山努亚和他的一千零一夜",
        "第02部：渔翁、魔鬼和四色鱼的故事",
        "第03部：懒汉克辽尼和铜城的故事",
        "第04部：朱特和两个哥哥的故事",
        "第05部：驼背的故事",
        "第06部：太子阿特士和公主哈娅·图芙丝之梦的故事",
        "第07部：终身不笑者的故事",
        "第08部：钱商和匪徒的故事",
        "第09部：哈·曼丁的故事",
        "第10部：乌木马的故事",
        "第11部：睡着的国王的故事",
        "第12部：洗染匠和理发师的故事",
        "第13部：撒谎者贝浩图的故事",
        "第14部：渔夫和雄人鱼的故事",
        "第15部：阿拉丁和神灯的故事",
        "第16部：航海家辛巴达的故事",
        "第17部：瞎眼僧人的故事",
        "第18部：智者盲老人的故事",
        "第19部：蠢汉、驴子与骗子的故事",
        "第20部：女王祖白绿和糖饭桌子的故事",
        "第21部：阿卜杜拉·法兹里和两个哥哥的故事",
        "第22部：麦仑·沙迈追求漂亮女人的故事",
        "第23部：补鞋匠迈尔鲁夫的故事",
        "第24部：巴士拉银匠哈桑的故事",
        "第25部：海姑娘和她儿子的故事",
        "第26部：阿里巴巴和四十大盗的故事。"
    });
    say(this_player()->query("name") + "开始阅读《一千零一夜》。\n");
    write("你开始阅读一千零一夜 " + element_of(capter) + "\n");
    return 1;
}

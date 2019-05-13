#include "mbed.h"
#include "SDBlockDevice.h"
#include "FATFileSystem.h"
#include "Dir.h"
#include <string>
#include <time.h>

using std::string;

#define SD_MOUNT_PATH           "sd"

#if !defined(POST_APPLICATION_ADDR)
#error "target.restrict_size must be set for your target in mbed_app.json"
#endif

//SD卡引脚定义
SDBlockDevice sd(MBED_CONF_APP_SD_CARD_MOSI, MBED_CONF_APP_SD_CARD_MISO,
                 MBED_CONF_APP_SD_CARD_SCK, MBED_CONF_APP_SD_CARD_CS);
FATFileSystem fs(SD_MOUNT_PATH);
FlashIAP flash;

//流程函数
string find_update(const char* path);
void apply_update(FILE *file, uint32_t address);

int main()
{
    printf("into bootloader\r\n");
    //step1:加载sd卡
    sd.init();
    int ret=fs.mount(&sd);
    if ( ret != 0 ){
        printf("mount fail\r\n");
    }
    else{
        printf("mount success\r\n");
    }
    //step2:找到最新的可更新固件
    string newest_firmware = find_update("");
    printf("newest: %s\r\n",newest_firmware.c_str());
    //step3:将固件写入当前激活应用区域
    string full_path="";
    full_path += "/sd/";
    full_path += newest_firmware;
    FILE *file = fopen(full_path.c_str(), "rb");
    if (file != NULL) {
        printf("Firmware update found\r\n");

        apply_update(file, POST_APPLICATION_ADDR);

        fclose(file);
        remove(full_path.c_str());
    } else {
        printf("No update found to apply\r\n");
    }
    fs.unmount();
    sd.deinit();
    //step4:将控制权交给当前激活应用
    printf("Starting application\r\n");
    mbed_start_application(POST_APPLICATION_ADDR);
}

string find_update(const char* path){
    string ret_firm="";
    string l_path=path;
    int nowtime=0;
    Dir mydir(&fs,path);
    struct dirent filename;
    while(1){
        struct stat file_info; //临时存储文件信息
        int ret=mydir.read(&filename);
        if(ret==0)
            break;
        // printf("name: %s \r\n",filename.d_name);
        string l_name = filename.d_name;
        string full_path = l_path + l_name;
        int ftime=fs.stat(full_path.c_str(),&file_info);
        if((file_info.st_mode & S_IFDIR) == S_IFDIR){
            continue;
            // printf("is a dir\r\n");
        }
        else{
            // printf("info: %d\r\n",ftime);
            if(ftime>nowtime){
                nowtime=ftime;
                ret_firm=l_name;
            }
        }
            
    }   
    mydir.close();
    return ret_firm;
}

void apply_update(FILE *file, uint32_t address)
{
    fseek(file, 0, SEEK_END);
    long len = ftell(file);
    printf("Firmware size is %ld bytes\r\n", len);
    fseek(file, 0, SEEK_SET);
  
    flash.init();

    const uint32_t page_size = flash.get_page_size();
    char *page_buffer = new char[page_size];
    uint32_t addr = address;
    uint32_t next_sector = addr + flash.get_sector_size(addr);
    bool sector_erased = false;
    size_t pages_flashed = 0;
    uint32_t percent_done = 0;
    while (true) {

        // 读取更新固件数据
        memset(page_buffer, 0, sizeof(page_buffer));
        int size_read = fread(page_buffer, 1, page_size, file);
        if (size_read <= 0) {
            break;
        }

        // 擦除目标数据
        if (!sector_erased) {
            flash.erase(addr, flash.get_sector_size(addr));
            sector_erased = true;
        }

        // 将更新固件写入闪存
        flash.program(page_buffer, addr, page_size);

        addr += page_size;
        if (addr >= next_sector) {
            next_sector = addr + flash.get_sector_size(addr);
            sector_erased = false;
        }

        if (++pages_flashed % 3 == 0) {
            uint32_t percent_done_new = ftell(file) * 100 / len;
            if (percent_done != percent_done_new) {
                percent_done = percent_done_new;
                printf("Flashed %3ld%%\r", percent_done);
            }
        }
    }
    printf("Flashed 100%%\r\n");

    delete[] page_buffer;

    flash.deinit();
}

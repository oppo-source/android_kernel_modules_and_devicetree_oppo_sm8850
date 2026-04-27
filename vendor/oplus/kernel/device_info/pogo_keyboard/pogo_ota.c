#include "pogo_keyboard.h"
#include "pogo_healthinfo.h"
#include "owb.h"
#include <linux/firmware.h>

struct dfu_ota_data {
    u32 checksum;
    u32 start_addr;
    u32 file_len;
    int type;
    unsigned char fw_ver[KBVER_LEN_MAX];
};
struct dfu_ota_data *bat_data = NULL;
struct dfu_ota_data *bin_data = NULL;
struct dfu_ota_data *tp_data = NULL;
struct dfu_ota_data *pt_data = NULL;

static int handle_firmware_validation(const struct firmware **fw_entry_ptr,
                                     u32 *fw_data_count, const unsigned char **firmware_data,
                                     u32 *checksum, int *version);
static int handle_dfu_ota_update(const unsigned char *firmware_data, u32 fw_data_count);
static int handle_standard_ota_update(const unsigned char *firmware_data, u32 fw_data_count,
                                     u32 checksum, int version);
static int handle_tp_ota_update(const unsigned char *firmware_data, u32 fw_data_count);
static int handle_kb_ota_update(const unsigned char *firmware_data, u32 fw_data_count);
static void cleanup_resources(const struct firmware *fw_entry);
static void cleanup_dfu_data(void);
static int kpd_fw_triple_isvalid(const unsigned char *fw_data, u32 count);

// calulator crc32
static u32 dfu_crc32(u8 const * p_data, size_t size, u32 const * p_crc)
{
    u32 crc = 0;
    u32 i = 0;
    crc = (p_crc == NULL) ? 0xFFFFFFFF : ~(*p_crc);
    for (i = 0; i < size; i++){
        crc = crc ^ p_data[i];
        for (u32 j = 8; j > 0; j--) crc = (crc >> 1) ^ (0xEDB88320U & ((crc & 1) ? 0xFFFFFFFF : 0));
    }
    return ~crc;
}

const struct firmware *get_fw_firmware(struct pogo_keyboard_data *pogo_data, const char *patch)
{
    struct platform_device *pdev = pogo_data->plat_dev;
    char *fw_patch = NULL;
    int retry = 2;
    int ret = 0;
    const struct firmware *fw_entry = NULL;

    fw_patch = kzalloc(MAX_FW_NAME_LENGTH, GFP_KERNEL);
    if(fw_patch == NULL)
        return NULL;

    snprintf(fw_patch, MAX_FW_NAME_LENGTH, "%s", patch);
    kb_info("fw_path is :%s\n", fw_patch);
    do {
        ret = request_firmware(&fw_entry, fw_patch, &pdev->dev);
        if(ret < 0) {
            kb_err("Failed to request fw\n");
            POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_FW_GET_FAIL);
            msleep(100);
        } else {
            break;
        }
    } while ((ret < 0) && (--retry > 0));

    kb_info("fw_path is :%s\n", fw_patch);
    kfree(fw_patch);
    return fw_entry;
}

static int kpd_fw_isvalid(const unsigned char *fw_data, u32 count, u32 *checksum, int *version)
{
    u32 start_addr = 0;
    u32 get_ver_addr = 0;
    u32 get_chechsum_addr = 0;
    u32 get_checksum = 0;
    u32 add_checksum = 0;
    u32 index = 0;
    u32 i = 0;

    start_addr = pogo_keyboard_client->ota_start_addr;
    get_ver_addr = pogo_keyboard_client->ota_get_version_addr;
    index = pogo_keyboard_client->ota_send_data_start_addr;
    i = index;
    get_chechsum_addr = get_ver_addr - 4;
    kb_info("start_addr:0x%x, get_ver_addr:0x%x, ota_send_data_start_addr:0x%x\n",
            start_addr, get_ver_addr, index);
    if (count < get_ver_addr | count < index) {
        kb_err("ota file count is too small,please check!!!\n");
        POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_FW_GET_FAIL);
        return -EINVAL;
    }
    do {
        add_checksum += (u32)fw_data[i];
    } while (++i < count);

    get_checksum = (u32)fw_data[get_chechsum_addr] |
                   (u32)fw_data[get_chechsum_addr + 1] << 8 |
                   (u32)fw_data[get_chechsum_addr + 2] << 16 |
                   (u32)fw_data[get_chechsum_addr + 3] << 24;
    if (get_checksum != add_checksum) {
        kb_debug("add_checksum:0x%08x, get_checksum:0x%08x\n", add_checksum, get_checksum);
        kb_err("ota file checksum is not right,please check!!!\n");
        POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_FW_GET_FAIL);
        return -EINVAL;
    }
    *checksum = get_checksum;

    pogo_keyboard_client->kpdmcu_fw_data_ver = (int)fw_data[get_ver_addr + 1] << 8 |
                                                (int)fw_data[get_ver_addr];
    *version = pogo_keyboard_client->kpdmcu_fw_data_ver;
    if (pogo_keyboard_client->kpdmcu_mcu_version < pogo_keyboard_client->kpdmcu_fw_data_ver) {
        pogo_keyboard_client->is_kpdmcu_need_fw_update = true;
    } else {
        pogo_keyboard_client->is_kpdmcu_need_fw_update = false;
    }
    kb_info("fw verison is 0x%04x\n", *version);
    return 0;
}


/*OTA update online
1.read mcu keyboard version
2.send ota file infomation
3.send ota file datas
4.send ota end infomation
5.ota end reset mcu
*/

static int pogo_keyboard_mcu_version(void)
{
    int ret = 0;
    char write_buf[] = { ONE_WIRE_BUS_PACKET_OTA_CMD, 0x03, 0x01, 0x01, 0x01};
    char buf[] = { ONE_WIRE_BUS_PACKET_OTA_ACK_CMD, 0x06, 0x01, 0x04};
    char read_buf[255] = { 0 };
    int read_len = 0;
    int i = 0;

    for (i = 0; i < 3; i++) {
        ret = pogo_keyboard_write_and_read(write_buf, sizeof(write_buf), read_buf, &read_len);
        if (ret) {
            continue;
        }
        if (memcmp(read_buf, buf, sizeof(buf)) == 0) {
            pogo_keyboard_client->kpdmcu_mcu_version = (read_buf[5] << 8) | read_buf[4];
            kb_info("kpdmcu_mcu_version:0x%x\n", pogo_keyboard_client->kpdmcu_mcu_version);
            break;
        } else {
            ret = -EINVAL;
        }
    }
    if (i >= 3) {
        kb_err("err ret:0x%02x\n", ret);
        pogo_keyboard_client->kpdmcu_mcu_version = 0;
        POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_OTA_FAIL);
        return ret;
    }
    return 0;
}

static int pogo_keyboard_ota_start_end(u32 len, u32 start_addr, u32 checksum, int version, bool start)
{
    int ret = 0;
    char write_buf[18] = { ONE_WIRE_BUS_PACKET_OTA_CMD, 0x10, 0x02, 0x0E};
    char buf[5] = { ONE_WIRE_BUS_PACKET_OTA_ACK_CMD, 0x04, 0x02, 0x02, 0x00};
    char read_buf[255] = { 0 };
    int read_len = 0;
    int i = 0;

    pm_stay_awake(&pogo_keyboard_client->plat_dev->dev);
    //ack len: ota start 0x04, ota end 0x07
    buf[1] = start ? 0x04 : 0x0b;
    //sub cmd : ota start 0x02, ota end 0x04
    write_buf[2] = start ? 0x02 : 0x04;
    buf[2] = start ? 0x02 : 0x04;
    //ack data len: ota start 0x02, ota end 0x05
    buf[3] = start ? 0x02 : 0x09;
    //ota file info data: 4byte len + 4byte start address + 1byte checksum + 2byte version
    write_buf[4] = (char)(len & 0xff);
    write_buf[5] = (char)((len & 0xff00) >> 8);
    write_buf[6] = (char)((len & 0xff0000) >> 16);
    write_buf[7] = (char)((len & 0xff000000) >> 24);
    write_buf[8] = (char)(start_addr & 0xff);
    write_buf[9] = (char)((start_addr & 0xff00) >> 8);
    write_buf[10] = (char)((start_addr & 0xff0000) >> 16);
    write_buf[11] = (char)((start_addr & 0xff000000) >> 24);
    write_buf[12] = (char)(checksum & 0xff);
    write_buf[13] = (char)((checksum & 0xff00) >> 8);
    write_buf[14] = (char)((checksum & 0xff0000) >> 16);
    write_buf[15] = (char)((checksum & 0xff000000) >> 24);
    write_buf[16] = (char)(version & 0xff);
    write_buf[17] = (char)((version & 0xff00) >> 8);

    for (i = 0; i < 3; i++) {
        ret = pogo_keyboard_write(write_buf, sizeof(write_buf));
        if (ret) {
            mdelay(50);
            continue;
        }
        mdelay(200);
        ret = pogo_keyboard_read(read_buf, &read_len);
        if (ret == 0) {
            if (memcmp(read_buf, buf, sizeof(buf)) == 0) {
                kb_info("send ota file infomation success.\n");
                break;
            } else {
                kb_err("read status:%d\n", read_buf[4]);
                ret = -EINVAL;
            }
        } else {
            mdelay(50);
            continue;
        }
    }
    if (i >= 3) {
        kb_err("err ret:0x%02x\n", ret);
        POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_OTA_FAIL);
        return ret;
    }
    pm_relax(&pogo_keyboard_client->plat_dev->dev);
    return 0;
}

static int pogo_keyboard_ota_write_datas(const unsigned char *fw_data, u32 len)
{
    int ret = 0;
    int i = 0;
    int j = 0;
    char write_buf[ONE_WRITY_LEN_MAX + 11] =
            { ONE_WIRE_BUS_PACKET_OTA_CMD, ONE_WRITY_LEN_MAX + 9, 0x03, ONE_WRITY_LEN_MAX + 7};
    char buf[11] = { ONE_WIRE_BUS_PACKET_OTA_ACK_CMD, 0x09, 0x03, 0x07};
    char read_buf[255] = { 0 };
    int read_len = 0;
    u32 write_len = 0, fw_len = 0, fw_offset = 0;
    char send_rx[ONE_WRITY_LEN_MAX] = {0};
    int package_index = 0;

    fw_len = len;
    while (fw_len) {
        write_len = (fw_len < ONE_WRITY_LEN_MAX) ? fw_len : ONE_WRITY_LEN_MAX;
        kb_debug("write_len:0x%08x\n", write_len);
        for(i = 0; i < write_len; i++) {
            send_rx[i] = fw_data[fw_offset + i];
        }
        if(fw_len < ONE_WRITY_LEN_MAX) {
            for(i = 0; i < ONE_WRITY_LEN_MAX - fw_len; i++) {
                //fill one package with 0x00
                send_rx[ONE_WRITY_LEN_MAX - i -1] = 0x00;
            }
        }
        write_buf[1] = (fw_len < ONE_WRITY_LEN_MAX) ? fw_len + 9 : ONE_WRITY_LEN_MAX + 9;
        write_buf[3] = (fw_len < ONE_WRITY_LEN_MAX) ? fw_len + 7 : ONE_WRITY_LEN_MAX + 7;
        //ota  write datas: 4byte offset address + 2byte package index + 1byte len + (n-7)byte datas
        write_buf[4] = (char)(fw_offset & 0xff);
        write_buf[5] = (char)((fw_offset & 0xff00) >> 8);
        write_buf[6] = (char)((fw_offset & 0xff0000) >> 16);
        write_buf[7] = (char)((fw_offset & 0xff000000) >> 24);
        write_buf[8] = (char)(package_index & 0xff);
        write_buf[9] = (char)((package_index & 0xff00) >> 8);
        write_buf[10] = write_len;
        memcpy(&write_buf[11], send_rx, sizeof(send_rx));

        for (j = 0; j < 3; j++) {
            ret = pogo_keyboard_write_and_read(write_buf, sizeof(write_buf), read_buf, &read_len);
            if (ret) {
                continue;
            }
            if (memcmp(read_buf, buf, 4) == 0) {
                if ((read_buf[4] == 0) &&
                    ((memcmp(&read_buf[5], &write_buf[8], 2)) == 0) &&
                    ((memcmp(&read_buf[7], &write_buf[4], 4)) == 0)) {
                    kb_debug("send ota data success, package_index:%d, fw_offset:0x%08x\n",
                        package_index, fw_offset);
                    break;
                } else {
                    pogo_keyboard_show_buf(read_buf, read_len);
                    kb_debug("send ota data ack status:%d\n", read_buf[4]);
                    ret = -EINVAL;
                }
            } else {
                ret = -EINVAL;
            }
        }
        if (j >= 3) {
            kb_err("err ret:0x%02x\n", ret);
            POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_OTA_FAIL);
            return ret;
        }

        package_index++;
        fw_offset += write_len;
        fw_len -= write_len;
        kb_debug("package_index:%d,fw_offset:0x%08x,fw_len:0x%08x\n", package_index, fw_offset, fw_len);
        if(pogo_keyboard_client->fw_update_progress < FW_PROGRESS_96 * FW_PERCENTAGE_100) {
            pogo_keyboard_client->fw_update_progress = FW_PROGRESS_3 * FW_PERCENTAGE_100 +
                FW_PROGRESS_93 * (len - fw_len) * FW_PERCENTAGE_100 / len;
        } else {
            pogo_keyboard_client->fw_update_progress = FW_PROGRESS_96 * FW_PERCENTAGE_100  +
                FW_PROGRESS_93 * (len - fw_len) * FW_PERCENTAGE_100 / len;
        }
    }

    return 0;
}

static int pogo_keyboard_ota_end_reset(void)
{
    int ret = 0;
    char write_buf[] = { ONE_WIRE_BUS_PACKET_OTA_CMD, 0x06, 0x05, 0x04, 0x57, 0x4e, 0x38, 0x30};
    char buf[] = { ONE_WIRE_BUS_PACKET_OTA_ACK_CMD, 0x03, 0x05, 0x01, 0x00};
    char read_buf[255] = { 0 };
    int read_len = 0;
    int i = 0;

    for (i = 0; i < 3; i++) {
        ret = pogo_keyboard_write_and_read(write_buf, sizeof(write_buf), read_buf, &read_len);
        if (ret) {
            continue;
        }
        if (memcmp(read_buf, buf, sizeof(buf)) == 0) {
            kb_info("success!!!\n");
            break;
        } else {
            kb_err("ota reset ack status:%d\n", read_buf[4]);
            ret = -EINVAL;
        }
    }
    if (i >= 3) {
        kb_err("err ret:0x%02x\n", ret);
        POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_OTA_FAIL);
        return ret;
    }
    return 0;
}

static int kpd_fw_update(const unsigned char *fw_data, u32 count, u32 checksum, int version)
{
    int ret = 0;
    int retry = 3;
    int i = 0;
    u32 start_addr = 0;
    u32 index = 0;
    u32 len = 0;

    start_addr = pogo_keyboard_client->ota_start_addr;
    index = pogo_keyboard_client->ota_send_data_start_addr;
    len =  count - index;
    ret = pogo_keyboard_mcu_version();
    if (ret) {
        kb_err("get mcu version fail\n");
        return ret;
    }
    ret = pogo_keyboard_ota_start_end(len, start_addr, checksum, version, 1);
    if (ret) {
        kb_err("send ota file infomation fail\n");
        return ret;
    }
    for (i = 0; i < retry; i++)
    {
        ret = pogo_keyboard_ota_write_datas(&fw_data[index], len);
        if (ret) {
            kb_err("ota write datas fail, retry: %d\n", i);
        } else {
            kb_debug("ota write datas success!!!\n");
            break;
        }
    }
    if (i >= retry) {
        kb_err("ota write datas fail\n");
        POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_OTA_FAIL);
        return ret;
    }
    ret = pogo_keyboard_ota_start_end(len, start_addr, checksum, version, 0);
    if (ret) {
        kb_err("send ota end infomation fail\n");
        POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_OTA_FAIL);
        return ret;
    }

    ret = pogo_keyboard_ota_end_reset();
    if (ret) {
        kb_err("send ota end reset fail\n");
        POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_OTA_FAIL);
        return ret;
    }

    return ret;
}

/*
dfu ota thread
1.send bat file
2.send TP file, do tp ota
3.send bat file, do kb ota
basic info: 64byte
fw info: (64byte)
         ID,file type(2byte),
         offset,file start address(4byte)
         length,file lenghth(4byte)
         digest,file digest(32byte)
         vesion len(1byte)
         version(Nbyte)
         reverse
*/
static int validate_fw_params(const unsigned char *fw_data, u32 addr, int ver_len)
{
    if (!fw_data || ver_len <= 0 || ver_len > KBVER_LEN_MAX) {
        kb_err("Invalid parameters: fw_data=%p, ver_len=%d\n", fw_data, ver_len);
        return -EINVAL;
    }

    if (addr + sizeof(unsigned char[DFU_FW_INFO_LEN]) > U32_MAX) {
        kb_err("Address overflow: addr=0x%x\n", addr);
        return -EINVAL;
    }

    return 0;
}

static int validate_fw_data(const struct dfu_ota_data *data)
{
    if (data->file_len == 0 || data->start_addr == 0) {
        kb_err("Invalid firmware data: start_addr=0x%x, file_len=0x%x\n",
               data->start_addr, data->file_len);
        return -EINVAL;
    }
    return 0;
}

static int extract_version_info(const unsigned char *fwinfo, int ver_len, char *fw_ver, size_t fw_ver_size)
{
    if (ver_len > fw_ver_size || (43 + ver_len) > DFU_FW_INFO_LEN) {
        kb_err("Version length exceeds bounds: ver_len=%d\n", ver_len);
        return -EINVAL;
    }

    memset(fw_ver, 0, fw_ver_size);
    memcpy(fw_ver, &fwinfo[43], ver_len);
    return 0;
}

static u32 calculate_checksum(const unsigned char *fw_data, const struct dfu_ota_data *data)
{
    u32 crc32 = 0;
    u32 i = 0;

    if (data->type == 0x04) { // tp file
        for (i = 0; i < data->file_len; i++) {
            crc32 += (u32)fw_data[data->start_addr + i];
        }
    } else {
        crc32 = dfu_crc32(&fw_data[data->start_addr], data->file_len, &crc32);
    }

    return crc32;
}

static inline struct dfu_ota_data *get_fw_data(const unsigned char *fw_data, u32 addr, int ver_len)
{
    unsigned char fwinfo[DFU_FW_INFO_LEN] = {0};
    struct dfu_ota_data *data = NULL;
    int ret = 0;

    // Validate input parameters
    ret = validate_fw_params(fw_data, addr, ver_len);
    if (ret < 0) {
        return NULL;
    }

    data = kzalloc(sizeof(*data), GFP_KERNEL);
    if (!data) {
        kb_err("Failed to allocate memory for dfu_ota_data\n");
        return NULL;
    }

    // Extract firmware information
    memcpy(fwinfo, &fw_data[addr], sizeof(fwinfo));
    data->type = (fwinfo[0] << 8) | fwinfo[1];
    data->start_addr = (fwinfo[2] << 24) | (fwinfo[3] << 16) | (fwinfo[4] << 8) | fwinfo[5];
    data->file_len = (fwinfo[6] << 24) | (fwinfo[7] << 16) | (fwinfo[8] << 8) | fwinfo[9];

    // Validate extracted data
    if (validate_fw_data(data) < 0) {
        kfree(data);
        return NULL;
    }
    //kb version format: KA030_A_1.0.4_XXX, 43+9-1
    //tp version format: TP_A_02 (54 50 5f 41 5f 30 32)(ascll)
    kb_info("Firmware info - start_addr: 0x%x, file_len: 0x%x, type: 0x%x\n",
            data->start_addr, data->file_len, data->type);

    // Extract version information
    if (extract_version_info(fwinfo, ver_len, data->fw_ver, sizeof(data->fw_ver)) < 0) {
        kfree(data);
        return NULL;
    }

    // Calculate checksum
    data->checksum = calculate_checksum(fw_data, data);

    kb_info("Firmware validated - start_addr: 0x%x, file_len: 0x%x, type: 0x%x, "
            "checksum: 0x%x, version: %s\n",
            data->start_addr, data->file_len, data->type, data->checksum, data->fw_ver);

    return data;
}

static void cleanup_dfu_data(void)
{
    if (bat_data) {
        kfree(bat_data);
        bat_data = NULL;
    }
    if (bin_data) {
        kfree(bin_data);
        bin_data = NULL;
    }
    if (tp_data) {
        kfree(tp_data);
        tp_data = NULL;
    }
    if (pt_data) {
        kfree(pt_data);
        pt_data = NULL;
    }
}

static int kpd_fw_dfu_isvalid(const unsigned char *fw_data, u32 count)
{
    u32 fwinfo_addr = 0;

    if (DFU_FW_INFO_LEN < 64) {
        kb_err("DFU_FW_INFO_LEN define too small,please check!!!\n");
        return -EINVAL;
    }

    if (count < DFU_FW_INFO_LEN * 3) {
        kb_err("ota file count is too small,please check!!!\n");
        return -EINVAL;
    }
    fwinfo_addr = pogo_keyboard_client->dfu_fwinfo_start_addr;

    bat_data = get_fw_data(fw_data, fwinfo_addr, 13);
    if (!bat_data || (bat_data->start_addr + bat_data->file_len) > count) {
        kb_err("bat_data is NULL, or ota file count < bat_start_addr + bat_len, please check!!!\n");
        return -EINVAL;
    }
    bin_data = get_fw_data(fw_data, fwinfo_addr + DFU_FW_INFO_LEN, 13);
    if (!bin_data || (bin_data->start_addr + bin_data->file_len) > count) {
        kb_err("bin_data is NULL, or ota file count < bin_start_addr + bin_len, please check!!!\n");
        return -EINVAL;
    }

    tp_data = get_fw_data(fw_data, fwinfo_addr + DFU_FW_INFO_LEN * 2, 7);
    if (!tp_data || (tp_data->start_addr + tp_data->file_len) > count) {
        kb_err("tp_data is NULL, or ota file count < tp_start_addr + tp_len, please check!!!\n");
        return -EINVAL;
    }

    //version format: KA030_A_1.0.4_XXX, 43+9-1
    if (memcmp(bat_data->fw_ver, bat_data->fw_ver, 13)) {
        kb_err("bat_fw_version != bin_fw_version, please check!!!\n");
        return -EINVAL;
    }
    pogo_keyboard_client->kpdmcu_fw_data_ver =
                (int)(bat_data->fw_ver[8] & 0x0f) << 8 |
                (int)(bat_data->fw_ver[10] & 0x0f) << 4 |
                (int)(bat_data->fw_ver[12] & 0x0f);
    kb_info("kpdmcu_fw_data_ver: 0x%x\n", pogo_keyboard_client->kpdmcu_fw_data_ver);

    //get tp version: TP_A_02 (54 50 5f 41 5f 30 32)(ascll)
    memset(pogo_keyboard_client->report_tpver, 0, sizeof(pogo_keyboard_client->report_tpver));
    memcpy(pogo_keyboard_client->report_tpver, tp_data->fw_ver,
            sizeof(pogo_keyboard_client->report_tpver));

    kb_info("tp_ver: 0x%x\n", tp_data->fw_ver[6] );
    if ((pogo_keyboard_client->kpdmcu_mcu_version < pogo_keyboard_client->kpdmcu_fw_data_ver) ||
       (tp_data->fw_ver[6] == 0x43)) {//1.0.7_TP_A_0C not support tp ota
        pogo_keyboard_client->is_kpdmcu_need_fw_update = true;
    } else {
        pogo_keyboard_client->is_kpdmcu_need_fw_update = false;
    }
    return 0;
}

static int pogo_keyboard_dfu_start_end(u32 len, u32 start_addr, u32 checksum, int type, bool start)
{
    int ret = 0;
    char write_buf[18] = { ONE_WIRE_BUS_PACKET_OTA_CMD, 0x10, 0x02, 0x0E};
    char buf[5] = { ONE_WIRE_BUS_PACKET_OTA_ACK_CMD, 0x04, 0x02, 0x02, 0x00};
    char read_buf[255] = { 0 };
    int read_len = 0;
    int i = 0;
    int j = 0;

    pm_stay_awake(&pogo_keyboard_client->plat_dev->dev);
    //ack len: ota start 0x04, ota end 0x07
    buf[1] = start ? 0x04 : 0x07;
    //sub cmd : ota start 0x02, ota end 0x04
    write_buf[2] = start ? 0x02 : 0x04;
    buf[2] = start ? 0x02 : 0x04;
    //ack data len: ota start 0x02, ota end 0x05
    buf[3] = start ? 0x02 : 0x05;
    //ota file info data: 4byte len + 4byte start address + 1byte checksum + 2byte type
    write_buf[4] = (char)(len & 0xff);
    write_buf[5] = (char)((len & 0xff00) >> 8);
    write_buf[6] = (char)((len & 0xff0000) >> 16);
    write_buf[7] = (char)((len & 0xff000000) >> 24);
    write_buf[8] = (char)(start_addr & 0xff);
    write_buf[9] = (char)((start_addr & 0xff00) >> 8);
    write_buf[10] = (char)((start_addr & 0xff0000) >> 16);
    write_buf[11] = (char)((start_addr & 0xff000000) >> 24);
    write_buf[12] = (char)(checksum & 0xff);
    write_buf[13] = (char)((checksum & 0xff00) >> 8);
    write_buf[14] = (char)((checksum & 0xff0000) >> 16);
    write_buf[15] = (char)((checksum & 0xff000000) >> 24);
    write_buf[16] = (char)(type & 0xff);
    write_buf[17] = (char)((type & 0xff00) >> 8);

    for (i = 0; i < 3; i++) {
        ret = pogo_keyboard_write(write_buf, sizeof(write_buf));
        if (ret) {
            mdelay(50);
            continue;
        }
        do {
            mdelay(100);
            ret = pogo_keyboard_read(read_buf, &read_len);
            if (ret == 0 && memcmp(read_buf, buf, sizeof(buf)) == 0) {
                break;
            } else {
                j++;
            }
        } while (j < 4);
        if (j >= 4) {
            kb_err("read status:%d\n", read_buf[4]);
            ret = -EINVAL;
            continue;
        } else {
            kb_info("send ota file infomation success.\n");
            break;
        }
    }
    if (i >= 3) {
        kb_err("err ret:0x%02x\n", ret);
        return ret;
    }
    pm_relax(&pogo_keyboard_client->plat_dev->dev);
    return 0;
}

static int pogo_keyboard_dfu_write_datas(const unsigned char *fw_data, u32 len)
{
    int ret = 0;
    int i = 0;
    int j = 0;
    u32 crc32 = 0;
    char write_buf[DFU_ONE_WRITY_LEN_MAX + 11] =
            { ONE_WIRE_BUS_PACKET_OTA_CMD, DFU_ONE_WRITY_LEN_MAX + 9, 0x03, DFU_ONE_WRITY_LEN_MAX + 7};
    char buf[11] = { ONE_WIRE_BUS_PACKET_OTA_ACK_CMD, 0x09, 0x03, 0x07};
    char read_buf[255] = { 0 };
    int read_len = 0;
    u32 write_len = 0, fw_len = 0, fw_offset = 0;
    char send_rx[DFU_ONE_WRITY_LEN_MAX] = {0};
    u8 package_index = 0;

    fw_len = len;
    while (fw_len) {
        write_len = (fw_len < DFU_ONE_WRITY_LEN_MAX) ? fw_len : DFU_ONE_WRITY_LEN_MAX;;
        for(i = 0; i < write_len; i++) {
            send_rx[i] = fw_data[fw_offset + i];
        }
        if(fw_len < DFU_ONE_WRITY_LEN_MAX) {
            for(i = 0; i < DFU_ONE_WRITY_LEN_MAX - fw_len; i++) {
                //fill one package with 0x00
                send_rx[DFU_ONE_WRITY_LEN_MAX - i -1] = 0x00;
            }
        }
        memcpy(&write_buf[11], send_rx, sizeof(send_rx));
        crc32 = dfu_crc32(&write_buf[11], write_len, &crc32);
        write_buf[1] = (fw_len < DFU_ONE_WRITY_LEN_MAX) ? fw_len + 9 : DFU_ONE_WRITY_LEN_MAX + 9;
        write_buf[3] = (fw_len < DFU_ONE_WRITY_LEN_MAX) ? fw_len + 7 : DFU_ONE_WRITY_LEN_MAX + 7;
        //ota  write datas: 4byte crc32 + 2byte package index + 1byte len + (n-7)byte datas
        write_buf[4] = (char)(crc32 & 0xff);
        write_buf[5] = (char)((crc32 & 0xff00) >> 8);
        write_buf[6] = (char)((crc32 & 0xff0000) >> 16);
        write_buf[7] = (char)((crc32 & 0xff000000) >> 24);
        write_buf[8] = (char)(package_index & 0xff);
        write_buf[9] = 0x00;
        write_buf[10] = write_len;

        for (j = 0; j < 3; j++) {
            ret = pogo_keyboard_write_and_read(write_buf, sizeof(write_buf), read_buf, &read_len);
            if (ret) {
                continue;
            }
            if (memcmp(read_buf, buf, 4) == 0) {
                if (read_buf[4] == 0 &&
                    ((memcmp(&read_buf[5], &write_buf[8], 1)) == 0)/* &&
                    ((memcmp(&read_buf[7], &write_buf[4], 4)) == 0)*/) {
                    kb_debug("send ota data success, package_index:%d, fw_offset:0x%08x\n",
                        package_index, fw_offset);
                    break;
                } else {
                    pogo_keyboard_show_buf(read_buf, read_len);
                    kb_err("send ota data ack status:%d\n", read_buf[4]);
                    ret = -EINVAL;
                }
            } else {
                ret = -EINVAL;
            }
        }

        if (j >= 3) {
            kb_err("err ret:0x%02x\n", ret);
            return ret;
        }

        package_index++;
        fw_offset += write_len;
        fw_len -= write_len;
        kb_debug("package_index:%d,fw_offset:0x%08x,fw_len:0x%08x\n", package_index, fw_offset, fw_len);
        if(pogo_keyboard_client->fw_update_progress < FW_PROGRESS_5 * FW_PERCENTAGE_100) {
            pogo_keyboard_client->fw_update_progress = FW_PROGRESS_3 * FW_PERCENTAGE_100 +
                FW_PROGRESS_2 * (len - fw_len) * FW_PERCENTAGE_100 / len;
        } else if(pogo_keyboard_client->fw_update_progress < FW_PROGRESS_50 * FW_PERCENTAGE_100) {
            pogo_keyboard_client->fw_update_progress = FW_PROGRESS_5 * FW_PERCENTAGE_100 +
                FW_PROGRESS_45 * (len - fw_len) * FW_PERCENTAGE_100 / len;
        } else if(pogo_keyboard_client->fw_update_progress < FW_PROGRESS_96 * FW_PERCENTAGE_100) {
            pogo_keyboard_client->fw_update_progress = FW_PROGRESS_50 * FW_PERCENTAGE_100 +
                FW_PROGRESS_46 * (len - fw_len) * FW_PERCENTAGE_100 / len;
        } else {
            pogo_keyboard_client->fw_update_progress = FW_PROGRESS_96 * FW_PERCENTAGE_100;
        }
    }

    return 0;
}

static int kpd_fw_dfu_update(const unsigned char *fw_data, u32 count, u32 checksum, u32 addr, int len, int type)
{
    int ret = 0;
    int retry = 3;
    int i = 0;

    ret = pogo_keyboard_dfu_start_end(len, 0, 0, type, 1);
    if (ret) {
        kb_err("send ota file infomation fail\n");
        return ret;
    }

    for (i = 0; i < retry; i++)
    {
        ret = pogo_keyboard_dfu_write_datas(&fw_data[addr], len);
        if (ret) {
            kb_err("ota write datas fail, retry: %d\n", i);
        } else {
            kb_err("ota write datas success!!!\n");
            break;
        }
    }

    if (i >= retry) {
        kb_err("ota write datas fail\n");
        return ret;
    }
    msleep(50);
    //send TP data or KB data end,set heartbeat timeout to 2s
    if (type != 1) {
        pogo_keyboard_client->max_disconnect_count = DFU_DISCONNECT_COUNT; //2s
    }
    ret = pogo_keyboard_dfu_start_end(len, 0, checksum, type, 0);
    if (ret) {
        kb_err("send ota end infomation fail\n");
        return ret;
    }
    return ret;
}

static int tp_dfu_start(void)
{
    int ret = 0;
    char write_buf[4 + TPVER_LEN] = { ONE_WIRE_BUS_PACKET_USER_GENERAL_CMD, TPVER_LEN + 2, 0x12, TPVER_LEN};
    char buf[4] = { ONE_WIRE_BUS_PACKET_USER_GENERAL_ACK_CMD, 0x03, 0x12, 0x01};
    char read_buf[255] = { 0 };
    int read_len = 0;
    int i = 0;
    int j = 0;

    pm_stay_awake(&pogo_keyboard_client->plat_dev->dev);
    memcpy(&write_buf[4], pogo_keyboard_client->report_tpver, TPVER_LEN);

    for (i = 0; i < 3; i++) {
        ret = pogo_keyboard_write(write_buf, sizeof(write_buf));
        if (ret) {
            mdelay(50);
            continue;
        }
        do {
            mdelay(100);
            ret = pogo_keyboard_read(read_buf, &read_len);
            kb_err("ret:0x%02x\n", ret);
            if (ret == 0 && memcmp(read_buf, buf, sizeof(buf)) == 0) {
                break;
            } else {
                j++;
            }
        } while (j < 4);
        if (j >= 4) {
            kb_err("read status:%d\n", read_buf[4]);
            ret = -EINVAL;
            continue;
        } else {
            kb_info("send ota file infomation success.\n");
            break;
        }
    }
    if (i >= 3) {
        kb_err("err ret:0x%02x\n", ret);
        return ret;
    }
    ret = read_buf[4];
    pm_relax(&pogo_keyboard_client->plat_dev->dev);
    return ret;
}

static int tp_dfu_write_datas(const unsigned char *fw_data, u32 len, int *count)
{
    int ret = 0;
    int i = 0;
    int j = 0;
    char write_buf[DFU_ONE_WRITY_LEN_MAX + 6] =
            {ONE_WIRE_BUS_PACKET_USER_GENERAL_CMD, DFU_ONE_WRITY_LEN_MAX + 4, 0x13, DFU_ONE_WRITY_LEN_MAX + 2};
    char buf[4] = { ONE_WIRE_BUS_PACKET_USER_GENERAL_ACK_CMD, 0x05, 0x13, 0x03};
    char read_buf[255] = { 0 };
    int read_len = 0;
    u32 write_len = 0, fw_len = 0, fw_offset = 0;
    char send_rx[DFU_ONE_WRITY_LEN_MAX] = {0};
    int package_index = 0x0000;

    fw_len = len;
    while (fw_len) {
        package_index++;
        write_len = (fw_len < DFU_ONE_WRITY_LEN_MAX) ? fw_len : DFU_ONE_WRITY_LEN_MAX;;
        for(i = 0; i < write_len; i++) {
            send_rx[i] = fw_data[fw_offset + i];
        }
        if(fw_len < DFU_ONE_WRITY_LEN_MAX) {
            for(i = 0; i < DFU_ONE_WRITY_LEN_MAX - fw_len; i++) {
                //fill one package with 0x00
                send_rx[DFU_ONE_WRITY_LEN_MAX - i -1] = 0x00;
            }
        }
        write_buf[4] = (char)(package_index & 0xff);
        write_buf[5] = (char)((package_index & 0xff00) >> 8);
        memcpy(&write_buf[6], send_rx, sizeof(send_rx));

        for (j = 0; j < 3; j++) {
            ret = pogo_keyboard_write_and_read(write_buf, sizeof(write_buf), read_buf, &read_len);
            if (ret) {
                continue;
            }
            if (memcmp(read_buf, buf, sizeof(buf)) == 0) {
                if ((read_buf[6] == 0x01) || (read_buf[6] == 0x03)) {
                    kb_debug("send ota data success, package_index:%d, fw_offset:0x%08x\n",
                        package_index, fw_offset);
                    break;
                } else {
                    pogo_keyboard_show_buf(read_buf, read_len);
                    kb_err("send ota data package_index %d loss:%d\n",
                        package_index, read_buf[6]);
                    ret = read_buf[6];
                    return ret;
                }
            } else {
                ret = -EINVAL;
            }
        }

        if (j >= 3) {
            kb_err("err ret:0x%02x\n", ret);
            return ret;
        }

        fw_offset += write_len;
        fw_len -= write_len;
        kb_debug("package_index:%d,fw_offset:0x%08x,fw_len:0x%08x\n", package_index, fw_offset, fw_len);
        if(pogo_keyboard_client->fw_update_progress < FW_PROGRESS_5 * FW_PERCENTAGE_100) {
            pogo_keyboard_client->fw_update_progress = FW_PROGRESS_3 * FW_PERCENTAGE_100 +
                FW_PROGRESS_2 * (len - fw_len) * FW_PERCENTAGE_100 / len;
        } else if(pogo_keyboard_client->fw_update_progress < FW_PROGRESS_25 * FW_PERCENTAGE_100) {
            pogo_keyboard_client->fw_update_progress = FW_PROGRESS_5 * FW_PERCENTAGE_100 +
                FW_PROGRESS_20 * (len - fw_len) * FW_PERCENTAGE_100 / len;
        } else {
            pogo_keyboard_client->fw_update_progress = FW_PROGRESS_25 * FW_PERCENTAGE_100;
        }
    }
    *count = package_index;
    return 0;
}

static int tp_dfu_end(int count, u32 checksum)
{
    int ret = 0;
    char write_buf[10] = { ONE_WIRE_BUS_PACKET_USER_GENERAL_CMD, 0x08, 0x14, 0x06};
    char buf[5] = { ONE_WIRE_BUS_PACKET_USER_GENERAL_ACK_CMD, 0x03, 0x14, 0x01, 0x02};
    char read_buf[255] = { 0 };
    int read_len = 0;
    int i = 0;
    int j = 0;

    pm_stay_awake(&pogo_keyboard_client->plat_dev->dev);
    write_buf[4] = (char)(count & 0xff);
    write_buf[5] = (char)((count & 0xff00) >> 8);
    write_buf[6] = (char)(checksum & 0xff);
    write_buf[7] = (char)((checksum & 0xff00) >> 8);
    write_buf[8] = (char)((checksum & 0xff0000) >> 16);
    write_buf[9] = (char)((checksum & 0xff000000) >> 24);

    for (i = 0; i < 3; i++) {
        ret = pogo_keyboard_write(write_buf, sizeof(write_buf));
        if (ret) {
            mdelay(50);
            continue;
        }
        do {
            mdelay(100);
            ret = pogo_keyboard_read(read_buf, &read_len);
            kb_err("ret:0x%02x\n", ret);
            if (ret == 0 && memcmp(read_buf, buf, sizeof(buf)) == 0) {
                break;
            } else {
                j++;
            }
        } while (j < 4);
        if (j >= 4) {
            kb_err("read status:%d\n", read_buf[4]);
            ret = -EINVAL;
            continue;
        } else {
            kb_debug("send tp file success.\n");
            break;
        }
    }
    if (i >= 3) {
        kb_err("err ret:0x%02x\n", ret);
        return ret;
    }

    pm_relax(&pogo_keyboard_client->plat_dev->dev);
    return 0;
}

static int tp_fw_dfu_update(const unsigned char *fw_data, u32 count, u32 checksum, u32 addr, int len)
{
    int ret = 0;
    int retry = 3;
    int i = 0;
    int tp_package_count = 0;
    int loss_count = 0;

do_restart:
    ret = tp_dfu_start();
    if (ret < 0) {
        kb_err("send ota file infomation fail\n");
        return ret;
    } else if (ret == 0x01) {
        kb_err("tp version is same,not need update\n");
        return ret;
    }

    for (i = 0; i < retry; i++)
    {
        ret = tp_dfu_write_datas(&fw_data[addr], len, &tp_package_count);
        if (ret < 0) {
            kb_err("ota write datas fail, retry: %d\n", i);
        } else if (ret == 0x02) {//loss package,restart
            kb_err("ota write datas loss %d\n", loss_count);
            loss_count++;
            if (loss_count > 3) {
                kb_err("ota write datas faild\n");
                POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_OTA_FAIL);
                return -1;
            }
            i = 0;
            goto do_restart;
        } else {
            kb_err("ota write datas success!!!\n");
            break;
        }
    }

    if (i >= retry) {
        kb_err("ota write datas fail\n");
        POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_OTA_FAIL);
        return ret;
    }
    msleep(50);
    pogo_keyboard_client->max_disconnect_count = DFU_DISCONNECT_COUNT; //2s

    ret = tp_dfu_end(tp_package_count, checksum);
    if (ret) {
        kb_err("send ota end infomation fail\n");
        return ret;
    }
    return ret;
}

static int handle_firmware_validation(const struct firmware **fw_entry_ptr,
                                     u32 *fw_data_count, const unsigned char **firmware_data,
                                     u32 *checksum, int *version)
{
    if (pogo_keyboard_client->ota_firmware_name == NULL) {
        kb_err("ota_firmware_name is NULL!!!\n");
        return -EINVAL;
    }

    *fw_entry_ptr = get_fw_firmware(pogo_keyboard_client, pogo_keyboard_client->ota_firmware_name);
    if (*fw_entry_ptr == NULL) {
        kb_err("fw request firmware fail\n");
        return -EINVAL;
    }

    *fw_data_count = (u32)(*fw_entry_ptr)->size;
    *firmware_data = (*fw_entry_ptr)->data;
    kb_info("fw count 0X%x\n", *fw_data_count);
    if (pogo_keyboard_client->pogopin_ota_dfu) {
        return kpd_fw_dfu_isvalid(*firmware_data, *fw_data_count);
    } else if (pogo_keyboard_client->pogopin_triple_ota) {
        return kpd_fw_triple_isvalid(*firmware_data, *fw_data_count);
    } else {
        return kpd_fw_isvalid(*firmware_data, *fw_data_count, checksum, version);
    }
}

static int handle_tp_ota_success(void)
{
    pogo_keyboard_client->max_disconnect_count = TP_OTA_START_DISCONNECT_COUNT; // 15s
    pogo_keyboard_client->tp_ota_status = OTA_STATUS_ACTIVE;

    // TP OTA need > 12s
    do {
        msleep(OTA_SLEEP_INTERVAL);
        pogo_keyboard_client->fw_update_progress += TP_OTA_PROGRESS_INCREMENT;
        kb_debug("fw_update_progress:%d\n", pogo_keyboard_client->fw_update_progress);
    } while ((pogo_keyboard_client->tp_ota_status == OTA_STATUS_ACTIVE) &&
            (pogo_keyboard_client->fw_update_progress <= FW_PROGRESS_50 * FW_PERCENTAGE_100) &&
            (pogo_keyboard_client->fw_update_progress >= FW_PROGRESS_25 * FW_PERCENTAGE_100));

    if ((pogo_keyboard_client->fw_update_progress < FW_PROGRESS_25 * FW_PERCENTAGE_100) ||
        (pogo_keyboard_client->fw_update_progress > FW_PROGRESS_50 * FW_PERCENTAGE_100)) {
        kb_info("maybe KB plugout or be changed\n");
        update_fw_status(pogo_keyboard_client, FW_UPDATE_FAIL);
        pogo_keyboard_client->fw_update_progress = FW_PROGRESS_0;
        pogo_keyboard_client->tp_ota_status = OTA_STATUS_INACTIVE;
        POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_OTA_FAIL);
        return -EIO;
    } else if (pogo_keyboard_client->tp_ota_status == OTA_STATUS_INACTIVE) {
        kb_info("tp ota success!!!\n");
        pogo_keyboard_client->max_disconnect_count = DEFAULT_DISCONNECT_COUNT;
    }

    return 0;
}

static int handle_tp_ota_failure(void)
{
    kb_err("tp firmware ota fail!!!\n");
    if (pogo_keyboard_client->kpdmcu_mcu_version > KBMCU_VESION_1_0_7) {
        update_fw_status(pogo_keyboard_client, FW_UPDATE_FAIL);
        pogo_keyboard_client->fw_update_progress = FW_PROGRESS_0;
        pogo_keyboard_client->max_disconnect_count = DEFAULT_DISCONNECT_COUNT;
        POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_OTA_FAIL);
        return -EIO;
    } else {
        kb_info("kb version < 1.0.8, pass tp ota!!!\n");
    }
    return 0;
}

static int handle_tp_ota_update(const unsigned char *firmware_data, u32 fw_data_count)
{
    int ret;

    pogo_keyboard_client->fw_update_progress = FW_PROGRESS_6 * FW_PERCENTAGE_100;
    ret = tp_fw_dfu_update(firmware_data, fw_data_count, tp_data->checksum,
                            tp_data->start_addr, tp_data->file_len);

    if (ret == 0) {
        return handle_tp_ota_success();
    } else if (ret < 0) {
        return handle_tp_ota_failure();
    }

    return 0;
}

static int handle_kb_ota_update(const unsigned char *firmware_data, u32 fw_data_count)
{
    int ret;

    pogo_keyboard_client->fw_update_progress = FW_PROGRESS_50 * FW_PERCENTAGE_100;
    ret = kpd_fw_dfu_update(firmware_data, fw_data_count, bin_data->checksum,
                          bin_data->start_addr, bin_data->file_len, bin_data->type);

    if (ret == 0) {
        pogo_keyboard_client->max_disconnect_count = DFU_RESET_DISCONNECT_COUNT; // 20s
        pogo_keyboard_client->dfu_boot = 1;

        // DFU boot need > 6s
        do {
            msleep(OTA_SLEEP_INTERVAL);
            pogo_keyboard_client->fw_update_progress += KB_OTA_PROGRESS_INCREMENT;
            kb_debug("fw_update_progress:%d\n", pogo_keyboard_client->fw_update_progress);
        } while ((pogo_keyboard_client->fw_update_progress <= FW_PROGRESS_99 * FW_PERCENTAGE_100) &&
                (pogo_keyboard_client->fw_update_progress >= FW_PROGRESS_96 * FW_PERCENTAGE_100));

        if (pogo_keyboard_client->fw_update_progress > FW_PROGRESS_99 * FW_PERCENTAGE_100) {
            kb_info("kb ota not rest,maybe KB plugout\n");
            update_fw_status(pogo_keyboard_client, FW_UPDATE_FAIL);
            pogo_keyboard_client->fw_update_progress = FW_PROGRESS_0;
            pogo_keyboard_client->max_disconnect_count = DEFAULT_DISCONNECT_COUNT;
            POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_OTA_FAIL);
            return -EIO;
        }
    }

    return ret;
}

static int handle_dfu_ota_update(const unsigned char *firmware_data, u32 fw_data_count)
{
    int ret;

    if (!bat_data || !bin_data || !tp_data) {
        kb_err("bat_data or bin_data or tp_data is NULL\n");
        return -EINVAL;
    }

    ret = kpd_fw_dfu_update(firmware_data, fw_data_count, bat_data->checksum,
                          bat_data->start_addr, bat_data->file_len, bat_data->type);
    if (ret != 0) {
        return ret;
    }

    ret = handle_tp_ota_update(firmware_data, fw_data_count);
    if (ret != 0) {
        return ret;
    }

    ret = handle_kb_ota_update(firmware_data, fw_data_count);
    return ret;
}

/*
pogopin_triple_ota
pt_data: pressure touch data
tp_data: touchpad data
bin_data: keyboard data
*/
// Helper function to validate firmware data
static int ota_validate_fw_data(const unsigned char *fw_data, u32 count,
                           u32 addr, int ver_len, struct dfu_ota_data **result,
                           const char *name)
{
    struct dfu_ota_data *data = get_fw_data(fw_data, addr, ver_len);

    if (!data) {
        kb_err("Failed to get %s\n", name);
        return -EINVAL;
    }

    if ((data->start_addr + data->file_len) > count) {
        kb_err("%s exceeds file bounds: start=0x%x, len=0x%x, count=0x%x\n",
               name, data->start_addr, data->file_len, count);
        kfree(data);
        return -EINVAL;
    }

    *result = data;
    return 0;
}

// Helper function to extract version with bounds checking
static int extract_keyboard_version(const struct dfu_ota_data *bin_data)
{
    if (!bin_data) {
        kb_err("bin_data is NULL for version extraction\n");
        return -EINVAL;;
    }
    if (sizeof(bin_data->fw_ver) <= 12) {
        kb_err("bin_data fw_ver array too small for version extraction\n");
        return -EINVAL;
    }

    pogo_keyboard_client->kpdmcu_fw_data_ver =
                (int)(bin_data->fw_ver[8] & 0x0f) << 8 |
                (int)(bin_data->fw_ver[10] & 0x0f) << 4 |
                (int)(bin_data->fw_ver[12] & 0x0f);

    kb_info("kpdmcu_fw_data_ver: 0x%x\n", pogo_keyboard_client->kpdmcu_fw_data_ver);
    return 0;
}

// Helper function to extract TP/PT version
static void extract_version_string(const struct dfu_ota_data *data,
                                  char *dest, size_t dest_size, const char *name, int *fw_ver)
{
    int version = 0;
    if (!data) {
        kb_err("data is NULL for %s version extraction\n", name);
        return;
    }
    if (sizeof(data->fw_ver) <= 6) {
        kb_err("data fw_ver array too small for version extraction\n");
        return;
    }
    memset(dest, 0, dest_size);
    memcpy(dest, data->fw_ver, min(dest_size, sizeof(data->fw_ver)));

    version = (int)(data->fw_ver[5] & 0x0f) << 4 |
            (int)(data->fw_ver[6] & 0x0f);

    if (fw_ver) {
        *fw_ver = version;
    }
    kb_info("%s fw version: 0x%x\n", name, *fw_ver);
}

static int validate_triple_fw_input(const unsigned char *fw_data, u32 count)
{
    if (!fw_data) {
        kb_err("fw_data is NULL\n");
        return -EINVAL;
    }

    if (DFU_FW_INFO_LEN < 64) {
        kb_err("DFU_FW_INFO_LEN define too small, please check!!!\n");
        return -EINVAL;
    }

    if (count < DFU_FW_INFO_LEN * 3) {
        kb_err("ota file count is too small, please check!!!\n");
        return -EINVAL;
    }

    return 0;
}

static int validate_all_firmware_data(const unsigned char *fw_data, u32 count, u32 fwinfo_addr,
                                     struct dfu_ota_data **local_bin_data,
                                     struct dfu_ota_data **local_tp_data,
                                     struct dfu_ota_data **local_pt_data)
{
    int ret = 0;

    ret = ota_validate_fw_data(fw_data, count, fwinfo_addr, 13, local_bin_data, "bin_data");
    if (ret < 0) {
        return ret;
    }

    ret = ota_validate_fw_data(fw_data, count, fwinfo_addr + DFU_FW_INFO_LEN, 7, local_tp_data, "tp_data");
    if (ret < 0) {
        return ret;
    }

    ret = ota_validate_fw_data(fw_data, count, fwinfo_addr + DFU_FW_INFO_LEN * 2, 7, local_pt_data, "pt_data");
    if (ret < 0) {
        return ret;
    }

    return 0;
}

static int extract_all_versions(struct dfu_ota_data *local_bin_data,
                               struct dfu_ota_data *local_tp_data,
                               struct dfu_ota_data *local_pt_data)
{
    int ret = 0;

    ret = extract_keyboard_version(local_bin_data);
    if (ret < 0) {
        return ret;
    }

    extract_version_string(local_tp_data, pogo_keyboard_client->report_tpver,
                          sizeof(pogo_keyboard_client->report_tpver), "tp",
                          &pogo_keyboard_client->kpdmcu_fw_tp_ver);

    extract_version_string(local_pt_data, pogo_keyboard_client->report_ptver,
                          sizeof(pogo_keyboard_client->report_ptver), "pt",
                          &pogo_keyboard_client->kpdmcu_fw_pt_ver);

    return 0;
}

static void cleanup_firmware_data(struct dfu_ota_data *local_bin_data,
                                 struct dfu_ota_data *local_tp_data,
                                 struct dfu_ota_data *local_pt_data)
{
    if (local_bin_data) kfree(local_bin_data);
    if (local_tp_data) kfree(local_tp_data);
    if (local_pt_data) kfree(local_pt_data);
}

static bool should_update_triple_firmware(void)
{
    // Check MCU version
    if (pogo_keyboard_client->kpdmcu_mcu_version < pogo_keyboard_client->kpdmcu_fw_data_ver) {
        return true;
    }

    // Check PT/TP versions only when MCU versions match
    if (pogo_keyboard_client->kpdmcu_mcu_version == pogo_keyboard_client->kpdmcu_fw_data_ver) {
        return (pogo_keyboard_client->kpdmcu_tp_version < pogo_keyboard_client->kpdmcu_fw_tp_ver) ||
               (pogo_keyboard_client->kpdmcu_pt_version < pogo_keyboard_client->kpdmcu_fw_pt_ver);
    }

    return false;
}

static int kpd_fw_triple_isvalid(const unsigned char *fw_data, u32 count)
{
    u32 fwinfo_addr = 0;
    struct dfu_ota_data *local_bin_data = NULL;
    struct dfu_ota_data *local_tp_data = NULL;
    struct dfu_ota_data *local_pt_data = NULL;
    int ret = -EINVAL;

    // Input validation
    ret = validate_triple_fw_input(fw_data, count);
    if (ret < 0) {
        return ret;
    }

    fwinfo_addr = pogo_keyboard_client->triple_ota_fwinfo_start_addr;

    // Validate all firmware data
    ret = validate_all_firmware_data(fw_data, count, fwinfo_addr,
                                    &local_bin_data, &local_tp_data, &local_pt_data);
    if (ret < 0) {
        goto cleanup;
    }

    // Extract versions
    ret = extract_all_versions(local_bin_data, local_tp_data, local_pt_data);
    if (ret < 0) {
        goto cleanup;
    }

    // Update global data pointers
    bin_data = local_bin_data;
    tp_data = local_tp_data;
    pt_data = local_pt_data;

    // Determine if update is needed
    pogo_keyboard_client->is_kpdmcu_need_fw_update = should_update_triple_firmware();
    pogo_keyboard_client->triple_pt_need_update = false;

    ret = 0;
    kb_info("Triple firmware validation completed successfully\n");
    return ret;

cleanup:
    cleanup_firmware_data(local_bin_data, local_tp_data, local_pt_data);
    return ret;
}

static int send_fw_datas_progress(u32 total_len, u32 remaining_len)
{
    u32 processed_len = total_len - remaining_len;
    int progress = 0;

    if (pogo_keyboard_client->fw_update_progress < FW_PROGRESS_10 * FW_PERCENTAGE_100) {
        progress = FW_PROGRESS_3 * FW_PERCENTAGE_100 +
                  FW_PROGRESS_7 * processed_len * FW_PERCENTAGE_100 / total_len;
    } else if (pogo_keyboard_client->fw_update_progress < FW_PROGRESS_30 * FW_PERCENTAGE_100) {
        progress = FW_PROGRESS_10 * FW_PERCENTAGE_100 +
                  FW_PROGRESS_20 * processed_len * FW_PERCENTAGE_100 / total_len;
    } else if (pogo_keyboard_client->fw_update_progress < FW_PROGRESS_50 * FW_PERCENTAGE_100) {
        progress = FW_PROGRESS_30 * FW_PERCENTAGE_100 +
                  FW_PROGRESS_20 * processed_len * FW_PERCENTAGE_100 / total_len;
    } else {
        progress = FW_PROGRESS_50 * FW_PERCENTAGE_100;
    }

    pogo_keyboard_client->fw_update_progress = progress;
    kb_debug("fw_update_progress:%d\n", pogo_keyboard_client->fw_update_progress);
    return 0;
}

static void fill_send_buffer(char *send_rx, const unsigned char *fw_data,
                            u32 fw_offset, u32 write_len, u32 fw_len)
{
    int i = 0;

    for (i = 0; i < write_len; i++) {
        send_rx[i] = fw_data[fw_offset + i];
    }

    if (fw_len < DFU_ONE_WRITY_LEN_MAX) {
        for (i = 0; i < DFU_ONE_WRITY_LEN_MAX - fw_len; i++) {
            send_rx[DFU_ONE_WRITY_LEN_MAX - i - 1] = 0x00;
        }
    }
}

static int validate_ota_command_params(u32 len, int read_retries, int read_delay_ms)
{
    if (len <= 0 || read_retries <= 0 || read_delay_ms <= 0) {
        kb_err("Invalid params: len=%d, retries=%d, delay=%d\n", len, read_retries, read_delay_ms);
        return -EINVAL;
    }
    return 0;
}

static void build_ota_command_buffer(char *write_buf, u32 len, u32 start_addr, u32 checksum, int type, bool start)
{
    // Initialize command buffer
    write_buf[0] = ONE_WIRE_BUS_PACKET_OTA_CMD;
    write_buf[1] = 0x10;
    write_buf[2] = start ? 0x02 : 0x04;
    write_buf[3] = 0x0E;

    // Pack firmware information
    write_buf[4] = (char)(len & 0xff);
    write_buf[5] = (char)((len & 0xff00) >> 8);
    write_buf[6] = (char)((len & 0xff0000) >> 16);
    write_buf[7] = (char)((len & 0xff000000) >> 24);
    write_buf[8] = (char)(start_addr & 0xff);
    write_buf[9] = (char)((start_addr & 0xff00) >> 8);
    write_buf[10] = (char)((start_addr & 0xff0000) >> 16);
    write_buf[11] = (char)((start_addr & 0xff000000) >> 24);
    write_buf[12] = (char)(checksum & 0xff);
    write_buf[13] = (char)((checksum & 0xff00) >> 8);
    write_buf[14] = (char)((checksum & 0xff0000) >> 16);
    write_buf[15] = (char)((checksum & 0xff000000) >> 24);
    write_buf[16] = (char)(type & 0xff);
    write_buf[17] = (char)((type & 0xff00) >> 8);
}

static void build_expected_ack_buffer(char *expected_ack, bool start)
{
    expected_ack[0] = ONE_WIRE_BUS_PACKET_OTA_ACK_CMD;
    expected_ack[1] = start ? 0x04 : 0x07;
    expected_ack[2] = start ? 0x02 : 0x04;
    expected_ack[3] = start ? 0x02 : 0x05;
    expected_ack[4] = 0x00;
}

static bool wait_for_ota_response(char *read_buf, int *read_len, const char *expected_ack,
                                 int read_retries, int read_delay_ms, int *status)
{
    int read_retry = 0;

    for (read_retry = 0; read_retry < read_retries; read_retry++) {
        mdelay(read_delay_ms);

        if (pogo_keyboard_read(read_buf, read_len) == 0 &&
            memcmp(read_buf, expected_ack, 5) == 0) {
            *status = read_buf[4];
            kb_info("kb ota info send success, status:0x%02x\n", *status);
            return true;
        }
    }

    return false;
}

static int send_ota_command_with_retry(const char *write_buf, int write_buf_size,
                                      const char *expected_ack, int read_retries, int read_delay_ms)
{
    int ret = 0;
    int retry_count = 0;
    char read_buf[UART_BUFFER_SIZE] = {0};
    int read_len = 0;
    bool success = false;
    int status = 0;

    for (retry_count = 0; retry_count < WRITE_MAX_RETRIES && !success; retry_count++) {
        // Send command
        ret = pogo_keyboard_write((char*)write_buf, write_buf_size);
        if (ret) {
            mdelay(WRITE_DELAY_MS);
            continue;
        }

        // Wait for response
        success = wait_for_ota_response(read_buf, &read_len, expected_ack,
                                       read_retries, read_delay_ms, &status);

        if (!success) {
            kb_err("kb ota info read failed, status:0x%02x, retry:%d\n",
                   read_buf[4], retry_count);
            ret = -EINVAL;
        }
    }

    if (!success) {
        kb_err("kb ota info send failed after %d retries, ret:0x%02x\n",
               WRITE_MAX_RETRIES, ret);
        return -EINVAL;
    }

    return status;
}

static int send_ota_kb_start_end_command(u32 len, u32 start_addr, u32 checksum, int type, bool start,
                            int read_retries, int read_delay_ms)
{
    int ret = 0;
    char write_buf[18] = {0};
    char expected_ack[5] = {0};

    // Validate input parameters
    ret = validate_ota_command_params(len, read_retries, read_delay_ms);
    if (ret < 0) {
        return ret;
    }

    pm_stay_awake(&pogo_keyboard_client->plat_dev->dev);

    // Build command and expected ACK buffers
    build_ota_command_buffer(write_buf, len, start_addr, checksum, type, start);
    build_expected_ack_buffer(expected_ack, start);

    // Send command with retry logic
    ret = send_ota_command_with_retry(write_buf, sizeof(write_buf), expected_ack,
                                     read_retries, read_delay_ms);

    pm_relax(&pogo_keyboard_client->plat_dev->dev);
    return ret;
}

static int send_kb_ota_package_with_retry(char *write_buf, int write_buf_size,
                                         char *expected_ack, int expected_ack_size,
                                         int package_index)
{
    char read_buf[UART_BUFFER_SIZE] = { 0 };
    int read_len = 0;
    int ret = 0;
    int retry_count = 0;

    for (retry_count = 0; retry_count < TRIPLE_KB_DATA_WRITE_AND_READ_MAX_RETRIES; retry_count++) {
        ret = pogo_keyboard_write_and_read(write_buf, write_buf_size, read_buf, &read_len);
        if (ret) {
            continue;
        }

        if (memcmp(read_buf, expected_ack, expected_ack_size) == 0) {
            if (read_buf[4] == 0 && (memcmp(&read_buf[5], &write_buf[8], 1)) == 0) {
                kb_debug("send ota data success, package_index:%d\n", package_index);
                return 0;
            } else {
                pogo_keyboard_show_buf(read_buf, read_len);
                kb_err("send ota data ack status:%d\n", read_buf[4]);
                return read_buf[4];
            }
        }
    }

    kb_err("send kb ota data err ret:0x%02x\n", ret);
    return ret;
}

static void prepare_kb_ota_package(char *write_buf, char *send_rx, u32 crc32,
                                  u8 package_index, u32 write_len, u32 fw_len)
{
    write_buf[0] = ONE_WIRE_BUS_PACKET_OTA_CMD;
    write_buf[1] = (fw_len < DFU_ONE_WRITY_LEN_MAX) ? fw_len + 9 : DFU_ONE_WRITY_LEN_MAX + 9;
    write_buf[2] = 0x03;
    write_buf[3] = (fw_len < DFU_ONE_WRITY_LEN_MAX) ? fw_len + 7 : DFU_ONE_WRITY_LEN_MAX + 7;
    write_buf[4] = (char)(crc32 & 0xff);
    write_buf[5] = (char)((crc32 & 0xff00) >> 8);
    write_buf[6] = (char)((crc32 & 0xff0000) >> 16);
    write_buf[7] = (char)((crc32 & 0xff000000) >> 24);
    write_buf[8] = (char)(package_index & 0xff);
    write_buf[9] = 0x00;
    write_buf[10] = write_len;
    memcpy(&write_buf[11], send_rx, DFU_ONE_WRITY_LEN_MAX);
}

static int send_ota_kb_datas_command(const unsigned char *fw_data, u32 len)
{
    int ret = 0;
    u32 crc32 = 0;
    u32 write_len = 0, fw_len = 0, fw_offset = 0;
    char write_buf[DFU_ONE_WRITY_LEN_MAX + 11] = {0};
    char expected_ack[4] = { ONE_WIRE_BUS_PACKET_OTA_ACK_CMD, 0x09, 0x03, 0x07};
    char send_rx[DFU_ONE_WRITY_LEN_MAX] = {0};
    u8 package_index = 0;

    if (!fw_data || len <= 0) {
        kb_err("Invalid params for ota kb datas: fw_data=%p, len=%d\n", fw_data, len);
        return -EINVAL;
    }

    fw_len = len;
    while (fw_len) {
        write_len = (fw_len < DFU_ONE_WRITY_LEN_MAX) ? fw_len : DFU_ONE_WRITY_LEN_MAX;

        fill_send_buffer(send_rx, fw_data, fw_offset, write_len, fw_len);
        crc32 = dfu_crc32(send_rx, write_len, &crc32);
        prepare_kb_ota_package(write_buf, send_rx, crc32, package_index, write_len, fw_len);

        ret = send_kb_ota_package_with_retry(write_buf, sizeof(write_buf),
                                            expected_ack, sizeof(expected_ack), package_index);
        if (ret) {
            return ret;
        }

        package_index++;
        fw_offset += write_len;
        fw_len -= write_len;
        kb_debug("package_index:%d,fw_offset:0x%08x,fw_len:0x%08x\n", package_index, fw_offset, fw_len);

        send_fw_datas_progress(len, fw_len);
    }

    return ret;
}

static bool validate_ota_info_params(const char *version_data, int version_len,
                                    int read_retries, int read_delay_ms)
{
    return version_data && version_len > 0 && read_retries > 0 &&
           read_delay_ms > 0 && version_len <= TPVER_LEN;
}

static int read_ota_info_response(char *read_buf, int *read_len,
                                 char *expected_ack, int expected_ack_size,
                                 int read_retries, int read_delay_ms)
{
    int ret = 0;
    int read_retry = 0;

    for (read_retry = 0; read_retry < read_retries; read_retry++) {
        mdelay(read_delay_ms);
        ret = pogo_keyboard_read(read_buf, read_len);
        if (ret == 0 && memcmp(read_buf, expected_ack, expected_ack_size) == 0) {
            return read_buf[4];
        }
    }
    return -EINVAL;
}

static int send_ota_info_with_retry(char *write_buf, int write_buf_size,
                                   char *expected_ack, int expected_ack_size,
                                   int read_retries, int read_delay_ms,
                                   const char *device_name)
{
    int ret = 0;
    int retry_count = 0;
    char read_buf[UART_BUFFER_SIZE] = { 0 };
    int read_len = 0;

    for (retry_count = 0; retry_count < WRITE_MAX_RETRIES; retry_count++) {
        ret = pogo_keyboard_write(write_buf, write_buf_size);
        if (ret) {
            mdelay(WRITE_DELAY_MS);
            continue;
        }

        ret = read_ota_info_response(read_buf, &read_len, expected_ack,
                                    expected_ack_size, read_retries, read_delay_ms);
        if (ret >= 0) {
            kb_info("%s OTA info send success, status:0x%02x\n", device_name, ret);
            return ret;
        }

        kb_err("%s OTA info read failed, status:0x%02x, retry:%d\n",
               device_name, read_buf[4], retry_count);
    }

    kb_err("%s OTA info send failed after %d retries, ret:0x%02x\n",
           device_name, WRITE_MAX_RETRIES, ret);
    return -EINVAL;
}

static int send_ota_info_command(enum ota_device_type device_type, const char *version_data, int version_len,
                                int read_retries, int read_delay_ms)
{
    char write_buf[4 + TPVER_LEN] = { ONE_WIRE_BUS_PACKET_USER_GENERAL_CMD, version_len + 2, device_type, version_len};
    char expected_ack[4] = { ONE_WIRE_BUS_PACKET_USER_GENERAL_ACK_CMD, 0x03, device_type, 0x01};
    const char *device_name = (device_type == OTA_DEVICE_TP) ? "TP" : "PT";
    int ret = 0;

    if (!validate_ota_info_params(version_data, version_len, read_retries, read_delay_ms)) {
        kb_err("Invalid version data for %s OTA: data=%p, len=%d, retries=%d, delay=%d\n",
               device_name, version_data, version_len, read_retries, read_delay_ms);
        return -EINVAL;
    }

    memcpy(&write_buf[4], version_data, version_len);
    pm_stay_awake(&pogo_keyboard_client->plat_dev->dev);

    ret = send_ota_info_with_retry(write_buf, sizeof(write_buf), expected_ack,
                                  sizeof(expected_ack), read_retries, read_delay_ms, device_name);

    pm_relax(&pogo_keyboard_client->plat_dev->dev);
    return ret;
}

static int send_ota_package_with_retry(enum ota_data_device_type device_type,
                                     char *write_buf, int write_buf_size,
                                     char *expected_ack, int expected_ack_size,
                                     int package_index)
{
    char read_buf[UART_BUFFER_SIZE] = { 0 };
    int read_len = 0;
    int ret = 0;
    int retry_count = 0;

    for (retry_count = 0; retry_count < WRITE_MAX_RETRIES; retry_count++) {
        ret = pogo_keyboard_write_and_read(write_buf, write_buf_size, read_buf, &read_len);
        if (ret) {
            continue;
        }

        if (memcmp(read_buf, expected_ack, expected_ack_size) == 0) {
            if ((read_buf[6] == 0x01) || (read_buf[6] == 0x03)) {
                kb_debug("send ota data success, device_type:0x%x, package_index:%d\n",
                    device_type, package_index);
                return 0;
            } else {
                pogo_keyboard_show_buf(read_buf, read_len);
                kb_err("send ota data device_type:0x%x package_index %d ack status:0x%x\n",
                    device_type, package_index, read_buf[6]);
                return read_buf[6];
            }
        }
    }

    kb_err("device_type:0x%x err ret:0x%02x\n", device_type, ret);
    return ret;
}

static void prepare_ota_package(char *write_buf, char *send_rx,
                               enum ota_data_device_type device_type,
                               int package_index)
{
    write_buf[0] = ONE_WIRE_BUS_PACKET_USER_GENERAL_CMD;
    write_buf[1] = DFU_ONE_WRITY_LEN_MAX + 4;
    write_buf[2] = device_type;
    write_buf[3] = DFU_ONE_WRITY_LEN_MAX + 2;
    write_buf[4] = (char)(package_index & 0xff);
    write_buf[5] = (char)((package_index & 0xff00) >> 8);
    memcpy(&write_buf[6], send_rx, DFU_ONE_WRITY_LEN_MAX);
}

static int send_ota_datas_command(enum ota_data_device_type device_type,
                                const unsigned char *fw_data, u32 len, int *count)
{
    int ret = 0;
    int package_index = 0x0000;
    u32 write_len = 0, fw_len = 0, fw_offset = 0;
    char write_buf[DFU_ONE_WRITY_LEN_MAX + 6] = {0};
    char expected_ack[4] = { ONE_WIRE_BUS_PACKET_USER_GENERAL_ACK_CMD, 0x05, device_type, 0x03};
    char send_rx[DFU_ONE_WRITY_LEN_MAX] = {0};
    const char *device_name = (device_type == OTA_DATA_DEVICE_TP) ? "TP" : "PT";

    if (!fw_data || !count || len <= 0) {
        kb_err("Invalid params for %s OTA: fw_data=%p, count=%p, len=%d\n",
               device_name, fw_data, count, len);
        return -EINVAL;
    }

    fw_len = len;
    while (fw_len) {
        package_index++;
        write_len = (fw_len < DFU_ONE_WRITY_LEN_MAX) ? fw_len : DFU_ONE_WRITY_LEN_MAX;

        fill_send_buffer(send_rx, fw_data, fw_offset, write_len, fw_len);
        prepare_ota_package(write_buf, send_rx, device_type, package_index);

        ret = send_ota_package_with_retry(device_type, write_buf, sizeof(write_buf),
                                         expected_ack, sizeof(expected_ack), package_index);
        if (ret) {
            return ret;
        }

        fw_offset += write_len;
        fw_len -= write_len;
        kb_debug("device_type:%d package_index:%d,fw_offset:0x%08x,fw_len:0x%08x\n",
            device_type, package_index, fw_offset, fw_len);

        send_fw_datas_progress(len, fw_len);
    }

    *count = package_index;
    return 0;
}

static bool validate_ota_end_params(int count, int read_retries, int read_delay_ms)
{
    return count >= 0 && read_retries > 0 && read_delay_ms > 0;
}

static void prepare_ota_end_command(char *write_buf, enum ota_end_device_type device_type,
                                   int count, u32 checksum)
{
    write_buf[0] = ONE_WIRE_BUS_PACKET_USER_GENERAL_CMD;
    write_buf[1] = TRIPLE_PT_TP_END_LEN - 2;
    write_buf[2] = device_type;
    write_buf[3] = TRIPLE_PT_TP_END_LEN - 4;
    write_buf[4] = (char)(count & 0xff);
    write_buf[5] = (char)((count & 0xff00) >> 8);
    write_buf[6] = (char)(checksum & 0xff);
    write_buf[7] = (char)((checksum & 0xff00) >> 8);
    write_buf[8] = (char)((checksum & 0xff0000) >> 16);
    write_buf[9] = (char)((checksum & 0xff000000) >> 24);
}

static void prepare_ota_end_ack(char *expected_ack, enum ota_end_device_type device_type)
{
    expected_ack[0] = ONE_WIRE_BUS_PACKET_USER_GENERAL_ACK_CMD;
    expected_ack[1] = TRIPLE_PT_TP_END_ACK_LEN - 2;
    expected_ack[2] = device_type;
    expected_ack[3] = TRIPLE_PT_TP_END_ACK_LEN - 4;
    expected_ack[4] = 0x02; // 0x00 data package num err;0x01 crc err; 0x02 success
}

static bool read_ota_end_response(char *read_buf, int *read_len,
                                 char *expected_ack, int expected_ack_size,
                                 int read_retries, int read_delay_ms)
{
    int read_retry = 0;
    int ret = 0;

    for (read_retry = 0; read_retry < read_retries; read_retry++) {
        mdelay(read_delay_ms);
        ret = pogo_keyboard_read(read_buf, read_len);
        if (ret == 0 && memcmp(read_buf, expected_ack, expected_ack_size) == 0) {
            return true;
        }
    }
    return false;
}

static int send_ota_end_with_retry(char *write_buf, int write_buf_size,
                                  char *expected_ack, int expected_ack_size,
                                  int read_retries, int read_delay_ms,
                                  const char *device_name)
{
    int ret = 0;
    int retry_count = 0;
    char read_buf[UART_BUFFER_SIZE] = { 0 };
    int read_len = 0;
    bool success = false;

    for (retry_count = 0; retry_count < WRITE_MAX_RETRIES; retry_count++) {
        ret = pogo_keyboard_write(write_buf, write_buf_size);
        if (ret) {
            mdelay(WRITE_DELAY_MS);
            continue;
        }

        success = read_ota_end_response(read_buf, &read_len, expected_ack,
                                       expected_ack_size, read_retries, read_delay_ms);
        if (success) {
            kb_debug("%s OTA end command success\n", device_name);
            return 0;
        }

        kb_err("%s OTA end read failed, status:0x%02x, retry:%d\n",
               device_name, read_buf[4], retry_count);
    }

    kb_err("%s OTA end failed after %d retries, ret:0x%02x\n",
           device_name, WRITE_MAX_RETRIES, ret);
    return -EINVAL;
}

static int send_ota_end_command(enum ota_end_device_type device_type,
                               int count, u32 checksum,
                               int read_retries, int read_delay_ms)
{
    char write_buf[TRIPLE_PT_TP_END_LEN];
    char expected_ack[TRIPLE_PT_TP_END_ACK_LEN];
    const char *device_name = (device_type == OTA_END_DEVICE_TP) ? "TP" : "PT";
    int ret = 0;

    if (!validate_ota_end_params(count, read_retries, read_delay_ms)) {
        kb_err("Invalid parameters for %s OTA end: count=%d, retries=%d, delay=%d\n",
               device_name, count, read_retries, read_delay_ms);
        return -EINVAL;
    }

    prepare_ota_end_command(write_buf, device_type, count, checksum);
    prepare_ota_end_ack(expected_ack, device_type);
    pm_stay_awake(&pogo_keyboard_client->plat_dev->dev);

    ret = send_ota_end_with_retry(write_buf, sizeof(write_buf), expected_ack,
                                 sizeof(expected_ack), read_retries, read_delay_ms, device_name);

    pm_relax(&pogo_keyboard_client->plat_dev->dev);
    return ret;
}

static int handle_ota_start_result(int ret, const ota_device_ops_t *ops)
{
    if (pogo_keyboard_client == NULL) {
        return -EINVAL;
    }
    if (ret < 0) {
        kb_err("send ota file infomation fail\n");
        POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_OTA_FAIL);
        return ret;
    } else if (ret == 0x01) {
        kb_err("%s version is same,not need update\n", ops->device_name);
        return 0;
    }
    if (!strncmp(ops->device_name, "pt", strlen(ops->device_name))) {
        pogo_keyboard_client->triple_pt_need_update = true;
    }

    return 1; // Continue with OTA
}

static bool is_package_loss_error(int ret)
{
    return (ret == 0x02 || ret == 0x03 || ret == 0x05);
}

static bool is_vcc_error(int ret)
{
    return (ret == 0x04);
}

static int handle_ota_write_result(int ret, int *loss_count, int *i)
{
    if (ret < 0) {
        kb_err("ota write datas fail, retry: %d\n", *i);
        return -1; // Continue retry
    } else if (is_package_loss_error(ret)) {
        kb_err("ota write datas loss %d\n", *loss_count);
        (*loss_count)++;
        if (*loss_count > 3) {
            kb_err("ota write datas faild\n");
            POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_OTA_FAIL);
            return -2; // Fatal error
        }
        *i = 0; // Reset retry counter
        return 1; // Restart
    } else if (is_vcc_error(ret)) {
        POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_HARDWARE_ERROR);
        return -2; // Fatal error
    } else {
        kb_err("ota write datas success!!!\n");
        return 0; // Success
    }
}

static int perform_ota_write_with_retry(const unsigned char *fw_data, u32 addr, int len,
                                       const ota_device_ops_t *ops, int *package_count)
{
    int ret = 0;
    int retry = 3;
    int i = 0;
    int loss_count = 0;
    int write_result = 0;

    for (i = 0; i < retry; i++) {
        ret = ops->ota_write_datas(&fw_data[addr], len, package_count);
        write_result = handle_ota_write_result(ret, &loss_count, &i);

        if (write_result == 0) {
            return 0; // Success
        } else if (write_result == -2) {
            POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_OTA_FAIL);
            return -1; // Fatal error
        } else if (write_result == 1) {
            return 1; // Restart
        }
        // Continue retry for write_result == -1
    }

    if (i >= retry) {
        kb_err("ota write datas fail\n");
        POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_OTA_FAIL);
        return -1; // Fatal error
    }

    return 0;
}

static int triple_device_data_send(const unsigned char *fw_data, u32 count, u32 checksum,
                                  u32 addr, int len, const ota_device_ops_t *ops)
{
    int ret = 0;
    int package_count = 0;

do_restart:
    // Handle OTA start
    ret = ops->ota_start(len);
    ret = handle_ota_start_result(ret, ops);
    if (ret <= 0) {
        return ret; // Either error or no update needed
    }

    // Perform OTA write with retry
    ret = perform_ota_write_with_retry(fw_data, addr, len, ops, &package_count);
    if (ret < 0) {
        return ret;
    } else if (ret == 1) {
        goto do_restart;
    }

    // Handle OTA end
    msleep(50);
    ret = ops->ota_end(len, package_count, checksum);
    if (ret) {
        kb_err("send ota end infomation fail\n");
        POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_OTA_FAIL);
        return ret;
    }

    return ret;
}

static int triple_kb_ota_start(u32 len)
{
    return send_ota_kb_start_end_command(len, 0, 0, 0x0002, 1, TRIPLE_KB_INFO_READ_MAX_RETRIES, READ_DELAY_MS);
}

static int triple_kb_ota_write_datas(const unsigned char *fw_data, u32 len, int *count)
{
    return send_ota_kb_datas_command(fw_data, len);
}

static int triple_kb_ota_end(u32 len, int count, u32 checksum)
{
    // TP: 40 retries, 100ms delay (faster response)
    return send_ota_kb_start_end_command(len, 0, checksum, 0x0002, 0, TRIPLE_KB_INFO_READ_MAX_RETRIES, READ_DELAY_MS);
}

static int triple_kb_data_send(const unsigned char *fw_data, u32 count, u32 checksum, u32 addr, int len)
{
    static const ota_device_ops_t kb_ops = {
        .ota_start = triple_kb_ota_start,
        .ota_write_datas = triple_kb_ota_write_datas,
        .ota_end = triple_kb_ota_end,
        .device_name = "kb"
    };
    return triple_device_data_send(fw_data, count, checksum, addr, len, &kb_ops);
}

static int triple_tp_ota_start(u32 len)
{
    return send_ota_info_command(OTA_DEVICE_TP,
                                pogo_keyboard_client->report_tpver,
                                TPVER_LEN, TRIPLE_TP_INFO_READ_MAX_RETRIES, READ_DELAY_MS);
}

static int triple_tp_ota_write_datas(const unsigned char *fw_data, u32 len, int *count)
{
    return send_ota_datas_command(OTA_DATA_DEVICE_TP, fw_data, len, count);
}

static int triple_tp_ota_end(u32 len, int count, u32 checksum)
{
    // TP: 40 retries, 100ms delay (faster response)
    return send_ota_end_command(OTA_END_DEVICE_TP, count, checksum, TRIPLE_TP_END_READ_MAX_RETRIES, READ_DELAY_MS);
}

static int triple_tp_data_send(const unsigned char *fw_data, u32 count, u32 checksum, u32 addr, int len)
{
    static const ota_device_ops_t tp_ops = {
        .ota_start = triple_tp_ota_start,
        .ota_write_datas = triple_tp_ota_write_datas,
        .ota_end = triple_tp_ota_end,
        .device_name = "tp"
    };
    return triple_device_data_send(fw_data, count, checksum, addr, len, &tp_ops);
}

static int triple_pt_ota_start(u32 len)
{
    return send_ota_info_command(OTA_DEVICE_PT,
                                pogo_keyboard_client->report_ptver,
                                TPVER_LEN, TRIPLE_PT_INFO_READ_MAX_RETRIES, READ_DELAY_MS);
}

static int triple_pt_ota_write_datas(const unsigned char *fw_data, u32 len, int *count)
{
    // PT: 4 retries, 100ms delay (slower response, more retries)
    return send_ota_datas_command(OTA_DATA_DEVICE_PT, fw_data, len, count);
}


static int triple_pt_ota_end(u32 len, int count, u32 checksum)
{
    // PT: 4 retries, 100ms delay (slower response, more retries)
    return send_ota_end_command(OTA_END_DEVICE_PT, count, checksum, TRIPLE_PT_END_READ_MAX_RETRIES, READ_DELAY_MS);
}

static int triple_pt_data_send(const unsigned char *fw_data, u32 count, u32 checksum, u32 addr, int len)
{
    static const ota_device_ops_t pt_ops = {
        .ota_start = triple_pt_ota_start,
        .ota_write_datas = triple_pt_ota_write_datas,
        .ota_end = triple_pt_ota_end,
        .device_name = "pt"
    };
    return triple_device_data_send(fw_data, count, checksum, addr, len, &pt_ops);
}

static int triple_enter_ota_mode(int *device_type)
{
    int ret = 0;
    char write_buf[4 + TPVER_LEN] = { ONE_WIRE_BUS_PACKET_USER_GENERAL_CMD, 0x03, 0x20, 0x01, 0x01};
    char expected_ack[4] = { ONE_WIRE_BUS_PACKET_USER_GENERAL_ACK_CMD, 0x04, 0x20, 0x02};
    char read_buf[UART_BUFFER_SIZE] = { 0 };
    int read_len = 0;
    int retry_count = 0;
    int read_retry = 0;
    int read_retries = 4;
    bool success = false;

    pm_stay_awake(&pogo_keyboard_client->plat_dev->dev);

    for (retry_count = 0; retry_count < WRITE_MAX_RETRIES && !success; retry_count++) {
        ret = pogo_keyboard_write(write_buf, sizeof(write_buf));
        if (ret) {
            mdelay(WRITE_DELAY_MS);
            continue;
        }
        for (read_retry = 0; read_retry < read_retries; read_retry++) {
            mdelay(READ_DELAY_MS);

            ret = pogo_keyboard_read(read_buf, &read_len);
            if (ret == 0 && memcmp(read_buf, expected_ack, sizeof(expected_ack)) == 0) {
                *device_type = read_buf[4];
                success = true;
                break;
            }
        }

        if (!success) {
            kb_err("read failed, retry:%d\n", retry_count);
            ret = -EINVAL;
        }
    }

    if (!success) {
        kb_err("read failed after %d retries, ret:0x%02x\n", WRITE_MAX_RETRIES, ret);
        POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_ENTER_OTA_FAIL);
    }

    pm_relax(&pogo_keyboard_client->plat_dev->dev);
    return ret;
}

static int triple_tp_ota_progress(void)
{
    atomic_set(&pogo_keyboard_client->triple_tp_status, TRIPLE_OTA_START);
    pogo_keyboard_client->fw_update_progress = FW_PROGRESS_51 * FW_PERCENTAGE_100;
    // 15s, need > 13s
    do {
        msleep(TRIPLE_TP_OTA_SLEEP_INTERVAL);
        pogo_keyboard_client->fw_update_progress += TRIPLE_TP_OTA_PROGRESS_INCREMENT;
        kb_debug("fw_update_progress:%d\n", pogo_keyboard_client->fw_update_progress);
    } while ((atomic_read(&pogo_keyboard_client->triple_tp_status) == TRIPLE_OTA_START) &&
            (pogo_keyboard_client->fw_update_progress <= FW_PROGRESS_70 * FW_PERCENTAGE_100) &&
            (pogo_keyboard_client->fw_update_progress >= FW_PROGRESS_51 * FW_PERCENTAGE_100) &&
            ((pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_CONNECT_STATUS) != 0));

    if (atomic_read(&pogo_keyboard_client->triple_tp_status) == TRIPLE_OTA_SUCCESS) {
        kb_info("tp ota success!!!\n");
    } else if (((pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_CONNECT_STATUS) == 0) ||
        (pogo_keyboard_client->fw_update_progress < FW_PROGRESS_51 * FW_PERCENTAGE_100) ||
        (pogo_keyboard_client->fw_update_progress > FW_PROGRESS_70 * FW_PERCENTAGE_100) ||
        (atomic_read(&pogo_keyboard_client->triple_tp_status) == TRIPLE_OTA_FAIL)) {
        kb_info("maybe KB plugout or be changed\n");
        update_fw_status(pogo_keyboard_client, FW_UPDATE_FAIL);
        pogo_keyboard_client->fw_update_progress = FW_PROGRESS_0;
        atomic_set(&pogo_keyboard_client->triple_tp_status, TRIPLE_OTA_READY);
        return -EIO;
    }

    return 0;
}

static int triple_pt_ota_progress(void)
{
    atomic_set(&pogo_keyboard_client->triple_pt_status, TRIPLE_OTA_START);
    pogo_keyboard_client->fw_update_progress = FW_PROGRESS_71 * FW_PERCENTAGE_100;
    // 12s, need > 10s
    do {
        msleep(TRIPLE_TP_OTA_SLEEP_INTERVAL);
        pogo_keyboard_client->fw_update_progress += TRIPLE_PT_OTA_PROGRESS_INCREMENT;
        kb_debug("fw_update_progress:%d\n", pogo_keyboard_client->fw_update_progress);
    } while ((atomic_read(&pogo_keyboard_client->triple_pt_status) == TRIPLE_OTA_START) &&
            (pogo_keyboard_client->fw_update_progress <= FW_PROGRESS_90 * FW_PERCENTAGE_100) &&
            (pogo_keyboard_client->fw_update_progress >= FW_PROGRESS_71 * FW_PERCENTAGE_100) &&
            ((pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_CONNECT_STATUS) != 0));

    if (atomic_read(&pogo_keyboard_client->triple_pt_status) == TRIPLE_OTA_SUCCESS) {
        kb_info("pt ota success!!!\n");
    } else if (((pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_CONNECT_STATUS) == 0) ||
        (pogo_keyboard_client->fw_update_progress < FW_PROGRESS_71 * FW_PERCENTAGE_100) ||
        (pogo_keyboard_client->fw_update_progress > FW_PROGRESS_90 * FW_PERCENTAGE_100) ||
        (atomic_read(&pogo_keyboard_client->triple_pt_status) == TRIPLE_OTA_FAIL)) {
        kb_info("maybe KB plugout or be changed\n");
        update_fw_status(pogo_keyboard_client, FW_UPDATE_FAIL);
        pogo_keyboard_client->fw_update_progress = FW_PROGRESS_0;
        atomic_set(&pogo_keyboard_client->triple_pt_status, TRIPLE_OTA_READY);
        return -EIO;
    }

    return 0;
}

static int triple_kb_ota_progress(void)
{
    atomic_set(&pogo_keyboard_client->triple_kb_status, TRIPLE_OTA_START);
    pogo_keyboard_client->fw_update_progress = FW_PROGRESS_91 * FW_PERCENTAGE_100;
    // 4s, need > 3s
    do {
        msleep(OTA_SLEEP_INTERVAL);
        pogo_keyboard_client->fw_update_progress += TRIPLE_KB_OTA_PROGRESS_INCREMENT;
        kb_debug("fw_update_progress:%d\n", pogo_keyboard_client->fw_update_progress);
    } while ((atomic_read(&pogo_keyboard_client->triple_kb_status) == TRIPLE_OTA_START) &&
            (pogo_keyboard_client->fw_update_progress <= FW_PROGRESS_99 * FW_PERCENTAGE_100) &&
            (pogo_keyboard_client->fw_update_progress >= FW_PROGRESS_91 * FW_PERCENTAGE_100) &&
            ((pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_CONNECT_STATUS) != 0));

    if (atomic_read(&pogo_keyboard_client->triple_kb_status) == TRIPLE_OTA_SUCCESS) {
        kb_info("kb ota success!!!\n");
    } else if (((pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_CONNECT_STATUS) == 0) ||
        (pogo_keyboard_client->fw_update_progress < FW_PROGRESS_91 * FW_PERCENTAGE_100) ||
        (pogo_keyboard_client->fw_update_progress > FW_PROGRESS_99 * FW_PERCENTAGE_100) ||
        (atomic_read(&pogo_keyboard_client->triple_kb_status) == TRIPLE_OTA_FAIL)) {
        kb_info("maybe KB plugout or be changed\n");
        update_fw_status(pogo_keyboard_client, FW_UPDATE_FAIL);
        pogo_keyboard_client->fw_update_progress = FW_PROGRESS_0;
        atomic_set(&pogo_keyboard_client->triple_kb_status, TRIPLE_OTA_READY);
        pogo_keyboard_client->max_disconnect_count = DEFAULT_DISCONNECT_COUNT;
        return -EIO;
    }

    return 0;
}

static int triple_ota_failure(void)
{
    kb_err("triple ota fail!!!\n");
    update_fw_status(pogo_keyboard_client, FW_UPDATE_FAIL);
    pogo_keyboard_client->fw_update_progress = FW_PROGRESS_0;
    pogo_keyboard_client->max_disconnect_count = DEFAULT_DISCONNECT_COUNT;
    return -EIO;
}

static int execute_device_sequence(const triple_device_info_t *devices, int count, ...)
{
    int ret = 0;
    int i;
    va_list args;
    int device_indices[MAX_DEVICES];

    if (!devices) {
        kb_err("devices pointer is NULL\n");
        return -EINVAL;
    }

    if (count <= 0 || count > ARRAY_SIZE(device_indices)) {
        kb_err("Invalid device count: %d (max: %zu)\n", count, ARRAY_SIZE(device_indices));
        return -EINVAL;
    }

    va_start(args, count);
    for (i = 0; i < count; i++) {
        device_indices[i] = va_arg(args, int);
        if (device_indices[i] < 0 || device_indices[i] >= MAX_DEVICES) {
            kb_err("Invalid device index: %d (valid: 0-%d)\n",
                    device_indices[i], MAX_DEVICES - 1);
            va_end(args);
            return -EINVAL;
        }
    }
    va_end(args);

    for (i = 0; i < count; i++) {
        int device_idx = device_indices[i];
        kb_info("Executing %s OTA progress...\n", devices[device_idx].name);
        ret = devices[device_idx].ota_progress();
        if (ret != 0) {
            kb_err("%s OTA progress failed, ret: %d\n", devices[device_idx].name, ret);
            POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_FW_UPDATE_FAIL);
            return ret;
        }
        kb_info("%s OTA progress completed\n", devices[device_idx].name);
    }

    return 0;
}

static int execute_triple_ota_sequence(int device_type, const triple_device_info_t *devices)
{
    int ret = 0;

    switch (device_type) {
    case TRIPLE_DEVICE_KB:
        kb_info("Executing KB OTA sequence\n");
        ret = devices[TRIPLE_DEVICE_KB].ota_progress();
        break;

    case TRIPLE_DEVICE_TP:
        if (pogo_keyboard_client->triple_pt_need_update) {
            kb_info("Executing TP+PT+KB OTA sequence\n");
            // TP -> PT -> KB sequence
            ret = execute_device_sequence(devices, 3,
                TRIPLE_DEVICE_TP, TRIPLE_DEVICE_PT, TRIPLE_DEVICE_KB);
        } else {
            kb_info("Executing TP+KB OTA sequence\n");
            // TP ->  KB sequence
            ret = execute_device_sequence(devices, 2,
                TRIPLE_DEVICE_TP, TRIPLE_DEVICE_KB);
        }
        break;

    case TRIPLE_DEVICE_PT:
        kb_info("Executing PT+KB OTA sequence\n");
        // PT -> KB sequence
        ret = execute_device_sequence(devices, 2,
            TRIPLE_DEVICE_PT, TRIPLE_DEVICE_KB);
        break;

    default:
        kb_err("Unknown device type: %d\n", device_type);
        ret = -EIO;
        break;
    }

    return ret;
}

static int validate_triple_ota_params(const unsigned char *firmware_data, u32 fw_data_count)
{
    if (!firmware_data || fw_data_count == 0) {
        kb_err("Invalid firmware data or count\n");
        return -EINVAL;
    }

    if (!bin_data || !tp_data || !pt_data) {
        kb_err("bin_data or tp_data or pt_data is NULL\n");
        return -EINVAL;
    }

    return 0;
}

static int send_firmware_data_to_devices(const unsigned char *firmware_data, u32 fw_data_count,
                                        const triple_device_info_t *devices,
                                        struct dfu_ota_data **data_ptrs, int device_count)
{
    int ret = 0;
    int i = 0;

    for (i = 0; i < device_count; i++) {
        if (!data_ptrs[i]) {
            kb_err("%s data is NULL\n", devices[i].name);
            return -EINVAL;
        }

        pogo_keyboard_client->fw_update_progress = devices[i].progress_value * FW_PERCENTAGE_100;

        kb_info("Sending %s firmware data...\n", devices[i].name);
        ret = devices[i].data_send(firmware_data, fw_data_count,
                                  data_ptrs[i]->checksum,
                                  data_ptrs[i]->start_addr,
                                  data_ptrs[i]->file_len);
        if (ret != 0) {
            kb_err("Failed to send %s firmware data, ret: %d\n", devices[i].name, ret);
            POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_FW_UPDATE_FAIL);
            return ret;
        }
        kb_info("%s firmware data sent successfully\n", devices[i].name);
    }

    return 0;
}

static int handle_triple_ota_update(const unsigned char *firmware_data, u32 fw_data_count)
{
    int ret = 0;
    int device_type = 0;
    static const triple_device_info_t devices[] = {
        {"kb", triple_kb_data_send, triple_kb_ota_progress, FW_PROGRESS_3},
        {"tp", triple_tp_data_send, triple_tp_ota_progress, FW_PROGRESS_10},
        {"pt", triple_pt_data_send, triple_pt_ota_progress, FW_PROGRESS_30}
    };
    int device_count = ARRAY_SIZE(devices);
    struct dfu_ota_data *data_ptrs[ARRAY_SIZE(devices)] = {NULL};

    // Validate input parameters
    ret = validate_triple_ota_params(firmware_data, fw_data_count);
    if (ret < 0) {
        return ret;
    }

    // Setup data pointers safely
    if (device_count >= 1) data_ptrs[0] = bin_data;
    if (device_count >= 2) data_ptrs[1] = tp_data;
    if (device_count >= 3) data_ptrs[2] = pt_data;

    // Send firmware data to all devices
    ret = send_firmware_data_to_devices(firmware_data, fw_data_count, devices,
                                       data_ptrs, device_count);
    if (ret != 0) {
        return ret;
    }

    // Enter OTA mode and detect device type
    pogo_keyboard_client->fw_update_progress = FW_PROGRESS_50 * FW_PERCENTAGE_100;
    ret = triple_enter_ota_mode(&device_type);
    if (ret != 0) {
        kb_err("Failed to enter OTA mode, ret: %d\n", ret);
        POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_FW_UPDATE_FAIL);
        return triple_ota_failure();
    }
    kb_debug("Device type detected: %d\n", device_type);
    ret = execute_triple_ota_sequence(device_type, devices);
    if (ret != 0) {
        kb_err("OTA sequence failed for device type %d, ret: %d\n", device_type, ret);
        POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_FW_UPDATE_FAIL);
        return ret;
    }

    kb_info("Triple OTA update completed successfully\n");
    return ret;
}

static int handle_standard_ota_update(const unsigned char *firmware_data, u32 fw_data_count,
                                     u32 checksum, int version)
{
    return kpd_fw_update(firmware_data, fw_data_count, checksum, version);
}

static void cleanup_resources(const struct firmware *fw_entry)
{
    if (fw_entry) {
        release_firmware(fw_entry);
    }
    cleanup_dfu_data();
}

void kpdmcu_fw_data_version_thread(struct work_struct *work)
{
    const struct firmware *fw_entry = NULL;
    u32 checksum = 0;
    int version = 0;
    int ret = 0;

    if (pogo_keyboard_client == NULL) {
        kb_err("pogo_keyboard_client is NULL!!!\n");
        return;
    }

    if (pogo_keyboard_client->ota_firmware_name == NULL) {
        kb_err("ota_firmware_name is NULL!!!\n");
        return;
    }

    cleanup_dfu_data();

    fw_entry = get_fw_firmware(pogo_keyboard_client, pogo_keyboard_client->ota_firmware_name);
    if (fw_entry != NULL) {
        pogo_keyboard_client->kpdmcu_fw_cnt = (u32)(fw_entry->size / 1024);
        kb_info("kpdmcu_fw_cnt:%uKB\n", pogo_keyboard_client->kpdmcu_fw_cnt);
        if (pogo_keyboard_client->pogopin_ota_dfu)
            ret = kpd_fw_dfu_isvalid(fw_entry->data, fw_entry->size);
        else if (pogo_keyboard_client->pogopin_triple_ota)
            ret = kpd_fw_triple_isvalid(fw_entry->data, fw_entry->size);
        else
            ret = kpd_fw_isvalid(fw_entry->data, fw_entry->size, &checksum, &version);
        if (ret) {
            kb_err("kpd fw is not valid!!!\n");
            POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_FW_UPDATE_FAIL);
            cleanup_dfu_data();
        }
    } else {
        kb_err("kpd mcu request firmware fail\n");
        POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_FW_UPDATE_FAIL);
        return;
    }

    release_firmware(fw_entry);
    fw_entry = NULL;
}

static int ensure_lcd_state(struct pogo_keyboard_data *client)
{
    int ret = 0;
    if ((client->pogo_keyboard_status & KEYBOARD_LCD_ON_STATUS) == 0) {
        ret = pogo_keyboard_set_lcd_state(true);
        if (ret) {
            kb_err("pogo_keyboard_set_lcd_state err!\n");
            update_fw_status(client, FW_UPDATE_FAIL);
            POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_FW_UPDATE_FAIL);
            return ret;
        }
    }
    return 0;
}

static bool should_skip_fw_update(void)
{
    return pogo_keyboard_client->is_kpdmcu_need_fw_update == false &&
           pogo_keyboard_client->kpdmcu_fw_update_force == false;
}

static int perform_fw_update(const unsigned char *firmware_data, u32 fw_data_count,
                           u32 checksum, int version)
{
    int ret = 0;

    if (ensure_lcd_state(pogo_keyboard_client) != 0) {
        return -EINVAL;
    }

    pogo_keyboard_client->fw_update_progress = FW_PROGRESS_2 * FW_PERCENTAGE_100;

    if (pogo_keyboard_client->pogopin_ota_dfu) {
        ret = handle_dfu_ota_update(firmware_data, fw_data_count);
    } else if (pogo_keyboard_client->pogopin_triple_ota) {
        ret = handle_triple_ota_update(firmware_data, fw_data_count);
    } else {
        ret = handle_standard_ota_update(firmware_data, fw_data_count, checksum, version);
    }

    return ret;
}

static void handle_fw_update_success(void)
{
    update_fw_status(pogo_keyboard_client, FW_UPDATE_SUC);
    pogo_keyboard_client->fw_update_progress = FW_PROGRESS_100 * FW_PERCENTAGE_100;
    pogo_keyboard_client->max_disconnect_count = DEFAULT_DISCONNECT_COUNT;
    kb_info("fw update success!\n");
}

static void handle_fw_update_failure(void)
{
    update_fw_status(pogo_keyboard_client, FW_UPDATE_FAIL);
}

void kpdmcu_fw_update_thread(struct work_struct *work)
{
    const unsigned char *firmware_data = NULL;
    u32 fw_data_count = 0;
    u32 checksum = 0;
    int version = 0;
    const struct firmware *fw_entry = NULL;
    int ret = 0;

    if (pogo_keyboard_client == NULL) {
        kb_err("pogo_keyboard_client is NULL\n");
        return;
    }
    update_fw_status(pogo_keyboard_client, FW_UPDATE_READY);
    if (pogo_keyboard_client->pogopin_wakelock)
        __pm_stay_awake(pogo_keyboard_client->pogopin_wakelock);
    mutex_lock(&pogo_keyboard_client->mutex);

    cleanup_dfu_data();

    update_fw_status(pogo_keyboard_client, FW_UPDATE_START);
    pogo_keyboard_client->kpdmcu_update_end = false;
    pogo_keyboard_client->fw_update_progress = FW_PROGRESS_1 * FW_PERCENTAGE_100;

    if (should_skip_fw_update()) {
        kb_info("not need fw update\n");
        handle_fw_update_success();
        goto cleanup;
    }

    ret = handle_firmware_validation(&fw_entry, &fw_data_count, &firmware_data, &checksum, &version);
    if (ret) {
        handle_fw_update_failure();
        goto cleanup;
    }

    ret = perform_fw_update(firmware_data, fw_data_count, checksum, version);
    if (ret) {
        handle_fw_update_failure();
        goto cleanup;
    }

    pogo_keyboard_client->kpdmcu_update_end = true;
    handle_fw_update_success();

cleanup:
    cleanup_resources(fw_entry);
    mutex_unlock(&pogo_keyboard_client->mutex);
    if (pogo_keyboard_client->pogopin_wakelock)
        __pm_relax(pogo_keyboard_client->pogopin_wakelock);
}

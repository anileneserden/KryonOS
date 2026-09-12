#include <kernel/fs/vfs.h>
#include <kernel/serial.h>
#include <kernel/string.h>

// A-Z arası sürücü tablosu (26 harf)
static vfs_mount_t mount_table[26];

void vfs_init(void) {
    for (int i = 0; i < 26; i++) {
        mount_table[i].is_mounted = false;
    }
    serial_write("VFS (Virtual File System) katmani baslatildi.\n");
}

bool vfs_mount(char drive_letter, const char* volume_name, fs_driver_t driver) {
    if (drive_letter >= 'a' && drive_letter <= 'z') {
        drive_letter -= 32; // Büyük harfe çevir
    }

    int index = drive_letter - 'A';
    if (index < 0 || index >= 26) return false;

    mount_table[index].drive_letter = drive_letter;
    mount_table[index].driver = driver;
    mount_table[index].is_mounted = true;

    int i = 0;
    while (volume_name[i] != '\0' && i < 31) {
        mount_table[index].volume_name[i] = volume_name[i];
        i++;
    }
    mount_table[index].volume_name[i] = '\0';

    serial_write("VFS: Surucu basariyla baglandi -> ");
    char dl_str[2] = { drive_letter, '\0' };
    serial_write(dl_str);
    serial_write(":\\\n");

    return true;
}

// "C:/dosya.txt" veya "C:\dosya.txt" yolundan harfi ve dosya adını ayırır
static bool parse_path(const char* full_path, char* out_drive, const char** out_filename) {
    if (!full_path || full_path[0] == '\0') return false;

    if (full_path[1] == ':' && (full_path[2] == '/' || full_path[2] == '\\')) {
        *out_drive = full_path[0];
        *out_filename = full_path + 3; // Harf ve ayırıcıyı atla
        return true;
    }
    return false;
}

void* vfs_read_file(const char* full_path, uint32_t* out_size) {
    char drive;
    const char* filename;

    if (!parse_path(full_path, &drive, &filename)) {
        serial_write("VFS Hata: Gecersiz yol formati! (Ornek: C:/dosya.txt)\n");
        if (out_size) *out_size = 0;
        return 0;
    }

    if (drive >= 'a' && drive <= 'z') drive -= 32;
    int index = drive - 'A';

    if (!mount_table[index].is_mounted) {
        serial_write("VFS Hata: Surucu takili degil!\n");
        if (out_size) *out_size = 0;
        return 0;
    }

    return mount_table[index].driver.read_file(filename, out_size);
}

void vfs_list_drive(char drive_letter) {
    if (drive_letter >= 'a' && drive_letter <= 'z') drive_letter -= 32;
    int index = drive_letter - 'A';

    if (!mount_table[index].is_mounted) {
        serial_write("VFS: Surucu takili degil.\n");
        return;
    }

    serial_write("\n========================================\n");
    serial_write(" Surucu ");
    char dl[2] = { mount_table[index].drive_letter, '\0' };
    serial_write(dl);
    serial_write(":\\ [ ");
    serial_write(mount_table[index].volume_name);
    serial_write(" ]\n");
    serial_write("========================================\n");

    if (mount_table[index].driver.list_dir) {
        mount_table[index].driver.list_dir();
    }
    serial_write("========================================\n\n");
}

int vfs_get_directory_files(const char* full_path, vfs_file_info_t* out_list, int max_count) {
    char drive;
    const char* rel_path;

    if (!parse_path(full_path, &drive, &rel_path)) {
        serial_write("VFS Hata: Gecersiz yol formati!\n");
        return 0;
    }

    if (drive >= 'a' && drive <= 'z') drive -= 32;
    int index = drive - 'A';

    if (!mount_table[index].is_mounted) {
        serial_write("VFS Hata: Surucu takili degil!\n");
        return 0;
    }

    if (mount_table[index].driver.get_dir_files) {
        return mount_table[index].driver.get_dir_files(rel_path, out_list, max_count);
    }

    return 0;
}
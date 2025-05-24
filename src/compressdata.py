import os
import gzip
import shutil

def gzip_file(source_path, dest_path):
    with open(source_path, 'rb') as f_in, gzip.open(dest_path, 'wb') as f_out:
        shutil.copyfileobj(f_in, f_out)

def gzip_all_files_in_dir(root_dir):
    for dirpath, _, filenames in os.walk(root_dir):
        for filename in filenames:
            if filename.endswith('.gz'):
                continue  # Pomijaj już skompresowane
            source_file = os.path.join(dirpath, filename)
            gz_file = source_file + '.gz'
            print(f"Kompresuję: {source_file} -> {gz_file}")
            gzip_file(source_file, gz_file)

if __name__ == "__main__":
    gzip_all_files_in_dir("data")
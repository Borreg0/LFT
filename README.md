# LFT
Local File Transfer is a small web app written in C for **Linux OS** to send files from any device connected to the local network to the host machine.

## Usage

- **Run LFT**: Terminal will open and show your IPv4 adress, port and instructions
- **Open Browser**: Paste the IP and port (xxx.xxx.xxx.xxx:8888) in your URL field    

## Compile yourself
- **Open terminal from LFT folder or navigate to it**: Write the following *cc Main.c -o LFT -I$PATH_TO_LIBMHD_INCLUDES -L$PATH_TO_LIBMHD_LIBS -lmicrohttpd*

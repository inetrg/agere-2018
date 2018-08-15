library(tikzDevice)
require(RColorBrewer)

df <- read.csv("evaluation/layers.csv", sep = ",", skip = 8)
# Clean empty columns.
emptycols <- sapply(df, function (k) all(is.na(k)))
df <- df[!emptycols]

# Separate test input size from name.
df$tmp_name <- data.frame(do.call('rbind', strsplit(as.character(df$name), '/', fixed=TRUE)))

# sepearate type from size
df$tmp_size <- data.frame(do.call('rbind', strsplit(as.character(df$tmp_name$X2), '_', fixed=TRUE)))

# Add a tag to differentiatae between udp and tcp tests.
df$benchmark <- df$tmp_name$X1
df$size <- df$tmp_size$X1
df$type <- df$tmp_size$X2

# drop weird ones
keeps <- c("benchmark", "size", "type", "real_time")
df <- df[keeps]

#  extract values
all_stddev <- split(df,df$type)[['stddev']]
all_median <- split(df,df$type)[['median']]
df <- split(df,df$type)[['mean']]

df$stddev <- all_stddev$real_time
df$median <- all_median$real_time

df$proto <- data.frame(ifelse(grepl("tcp", df$benchmark), "tcp", "udp"))

df$benchmark <- as.character(df$benchmark)
df$size <- as.numeric(as.character(df$size))


# Get UDP related data.
udp <- split(df,df$proto)[['udp']]
# Prepare splitting data into send and receive by adding a tag.
udp$operation <- data.frame(ifelse(grepl("send", udp$benchmark), "send", "receive"))
# Get UDP send tests.
udp_send <- split(udp,udp$operation)[['send']]
udp_send$benchmark <- gsub("BM_send<raw_data_message, udp_protocol<raw>>",                     "Raw",             udp_send$benchmark)
udp_send$benchmark <- gsub("BM_send<raw_data_message, udp_protocol<ordering<raw>>>",           "Ordering",        udp_send$benchmark)
udp_send$benchmark <- gsub("BM_send<new_basp_message, udp_protocol<datagram_basp>>",           "BASP",            udp_send$benchmark)
udp_send$benchmark <- gsub("BM_send<new_basp_message, udp_protocol<ordering<datagram_basp>>>", "Ordering + BASP", udp_send$benchmark)
udp_send_plot <- ggplot(udp_send, aes(x=size, y=real_time / 1000, color=benchmark)) +
                 geom_line() +
                 geom_point(aes(shape=benchmark)) +
                 scale_shape_manual(values = c(0, 1, 2, 3)) +
                 theme_bw() +
                 theme(
                   legend.title = element_blank(),
                   legend.position = "top",
                   legend.margin=margin(0,0,0,0),
                   legend.box.margin=margin(-10,-10,-10,-10),
                   text=element_text(size=9)
                 ) +
                 scale_color_brewer(type = "qual", palette = 7) +
                 labs(x="Payload Size [bytes]", y="Time [us]")
ggsave("figs/udp_send.pdf", plot=udp_send_plot, width=3.4, height=2.3)
### tikz export
tikz(file = "figs/udp_send.tikz", sanitize=TRUE, width=3.4, height=2.3)
udp_send_plot
dev.off()

# Get UDP receive tests.
udp_receive <- split(udp,udp$operation)[['receive']]
# Prepare splitting by sequence / single tests.
udp_receive$sequence <- data.frame(ifelse(grepl("sequence", udp_receive$benchmark), "yes", "no"))
# Process single results.
udp_receive_single <- split(udp_receive,udp_receive$sequence)[['no']]
udp_receive_single$benchmark <- gsub("BM_receive<raw_data_message, udp_protocol<raw>>",                     "Raw",             udp_receive_single$benchmark)
udp_receive_single$benchmark <- gsub("BM_receive<raw_data_message, udp_protocol<ordering<raw>>>",           "Ordering",        udp_receive_single$benchmark)
udp_receive_single$benchmark <- gsub("BM_receive<new_basp_message, udp_protocol<datagram_basp>>",           "BASP",            udp_receive_single$benchmark)
udp_receive_single$benchmark <- gsub("BM_receive<new_basp_message, udp_protocol<ordering<datagram_basp>>>", "Ordering + BASP", udp_receive_single$benchmark)
# Had to rename some benchmarks.
udp_receive_single$benchmark <- gsub("BM_receive_udp_raw",           "Raw",             udp_receive_single$benchmark)
udp_receive_single$benchmark <- gsub("BM_receive_udp_ordering_raw",  "Ordering",        udp_receive_single$benchmark)
udp_receive_single$benchmark <- gsub("BM_receive_udp_basp",          "BASP",            udp_receive_single$benchmark)
udp_receive_single$benchmark <- gsub("BM_receive_udp_ordering_basp", "Ordering + BASP", udp_receive_single$benchmark)
udp_receive_single_plot <- ggplot(udp_receive_single, aes(x=name$X2, y=real_time / 1000, color=benchmark)) +
                           geom_line() +
                           geom_point(aes(shape=benchmark)) +
                           scale_shape_manual(values = c(0, 1, 2, 3)) +
                           theme_bw() +
                           theme(
                             legend.title = element_blank(),
                             legend.position = "top",
                             legend.margin=margin(0,0,0,0),
                             legend.box.margin=margin(-10,-10,-10,-10),
                             text=element_text(size=9)
                           ) +
                           scale_color_brewer(type = "qual", palette = 7) +
                           labs(x="Payload Size [bytes]", y="Time [us]")
ggsave("figs/udp_receive_single.pdf", plot=udp_receive_single_plot, width=3.4, height=2.3)
### tikz export
tikz(file = "figs/udp_receive_single.tikz", sanitize=TRUE, width=3.4, height=2.3)
udp_receive_single_plot
dev.off()

# Process sequence results.
udp_receive_sequence <- split(udp_receive,udp_receive$sequence)[['yes']]
udp_receive_sequence$benchmark <- gsub("BM_receive_udp_raw_sequence_inorder", "Inorder",  udp_receive_sequence$benchmark)
udp_receive_sequence$benchmark <- gsub("BM_receive_udp_raw_sequence_dropped", "Dropped", udp_receive_sequence$benchmark)
udp_receive_sequence$benchmark <- gsub("BM_receive_udp_raw_sequence_late",    "Late", udp_receive_sequence$benchmark)
udp_receive_sequence_plot <- ggplot(udp_receive_sequence, aes(x=name$X2, y=real_time / 1000, color=benchmark)) +
                                    geom_line() +
                                    geom_point(aes(shape=benchmark)) +
                                    scale_shape_manual(values = c(4, 5, 6)) +
                                    scale_y_continuous(limits = c(0, 11.2), breaks = seq(0, 10, 2)) + 
                                    theme_bw() +
                                    theme(
                                      legend.title = element_blank(),
                                      legend.position = "top",
                                      legend.margin=margin(0,0,0,0),
                                      legend.box.margin=margin(-10,-10,-10,-10),
                                      text=element_text(size=9)
                                    ) +
                                    scale_color_brewer(type = "qual", palette = 7) +
                                    labs(x="Payload Size [bytes]", y="Time [us]")
ggsave("figs/udp_receive_sequence.pdf", plot=udp_receive_sequence_plot, width=3.4, height=2.3)
### tikz export
tikz(file = "figs/udp_receive_sequence.tikz", sanitize=TRUE, width=3.4, height=2.3)
udp_receive_sequence_plot
dev.off()

# Get tcp related data.
tcp <- split(df,df$proto)[['tcp']]
# Rename things.
tcp$benchmark <- gsub("BM_send<raw_data_message, tcp_protocol<raw>>",         "Raw",  tcp$benchmark)
tcp$benchmark <- gsub("BM_send<new_basp_message, tcp_protocol<stream_basp>>", "BASP", tcp$benchmark)

colors <- brewer.pal(n = 7, "Oranges")[3:9]

tcp_plot <- ggplot(tcp, aes(x=name$X2, y=real_time / 1000, color=benchmark)) +
                   geom_line() +
                   geom_point(aes(shape=benchmark)) +
                   scale_shape_manual(values = c(0, 3)) +
                   theme_bw() +
                   theme(
                     legend.title = element_blank(),
                     legend.position = "top",
                     legend.margin=margin(0,0,0,0),
                     legend.box.margin=margin(-10,-10,-10,-10),
                     text=element_text(size=9)
                   ) +
                   # scale_color_brewer(type = "qual", palette = 7) +
                   scale_colour_manual(values = brewer.pal(n=4, name = "Set2")[-c(2,3)]) + 
                   labs(x="Payload Size [bytes]", y="Time [us]")

basp <- split(tcp,tcp$benchmark)[['BASP']]
raw <- split(tcp,tcp$benchmark)[['Raw']]

diffs <- basp$real_time - raw$real_time

### pdf export
ggsave("figs/tcp_send.pdf", plot=tcp_plot, width=3.4, height=2.3)
### tikz export
tikz(file = "figs/tcp_send.tikz", sanitize=TRUE, width=3.4, height=2.3)
tcp_plot
dev.off()


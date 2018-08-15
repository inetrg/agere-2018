library(tikzDevice)

df <- read.csv("evaluation/layers.csv", sep = ",", skip = 8)
# Clean empty columns.
emptycols <- sapply(df, function (k) all(is.na(k)))
df <- df[!emptycols]
# Add a tag to differentiatae between udp and tcp tests.
df$proto <- data.frame(ifelse(grepl("udp", df$name), "udp", "tcp"))
# Separate test input size from name.
df$name <- data.frame(do.call('rbind', strsplit(as.character(df$name), '/', fixed=TRUE)))

# Get UDP related data.
udp <- split(df,df$proto)[['udp']]
# Convert Size value to numerics
udp$name$X1 <- as.character(udp$name$X1)
udp$name$X2 <- as.numeric(as.character(udp$name$X2))
# Prepare splitting data into send and receive by adding a tag.
udp$operation <- data.frame(ifelse(grepl("send", udp$name$X1), "send", "receive"))
# Get UDP send tests.
udp_send <- split(udp,udp$operation)[['send']]
udp_send$name$X1 <- gsub("BM_send<raw_data_message, udp_protocol<raw>>",                     "Raw",             udp_send$name$X1)
udp_send$name$X1 <- gsub("BM_send<raw_data_message, udp_protocol<ordering<raw>>>",           "Ordering",        udp_send$name$X1)
udp_send$name$X1 <- gsub("BM_send<new_basp_message, udp_protocol<datagram_basp>>",           "BASP",            udp_send$name$X1)
udp_send$name$X1 <- gsub("BM_send<new_basp_message, udp_protocol<ordering<datagram_basp>>>", "Ordering + BASP", udp_send$name$X1)
udp_send_plot <- ggplot(udp_send, aes(x=name$X2, y=real_time / 1000, color=name$X1)) +
                 geom_line() +
                 geom_point(aes(shape=name$X1)) +
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
udp_receive$sequence <- data.frame(ifelse(grepl("sequence", udp_receive$name$X1), "yes", "no"))
# Process single results.
udp_receive_single <- split(udp_receive,udp_receive$sequence)[['no']]
udp_receive_single$name$X1 <- gsub("BM_receive<raw_data_message, udp_protocol<raw>>",                     "Raw",             udp_receive_single$name$X1)
udp_receive_single$name$X1 <- gsub("BM_receive<raw_data_message, udp_protocol<ordering<raw>>>",           "Ordering",        udp_receive_single$name$X1)
udp_receive_single$name$X1 <- gsub("BM_receive<new_basp_message, udp_protocol<datagram_basp>>",           "BASP",            udp_receive_single$name$X1)
udp_receive_single$name$X1 <- gsub("BM_receive<new_basp_message, udp_protocol<ordering<datagram_basp>>>", "Ordering + BASP", udp_receive_single$name$X1)
# Had to rename some benchmarks.
udp_receive_single$name$X1 <- gsub("BM_receive_udp_raw",           "Raw",             udp_receive_single$name$X1)
udp_receive_single$name$X1 <- gsub("BM_receive_udp_ordering_raw",  "Ordering",        udp_receive_single$name$X1)
udp_receive_single$name$X1 <- gsub("BM_receive_udp_basp",          "BASP",            udp_receive_single$name$X1)
udp_receive_single$name$X1 <- gsub("BM_receive_udp_ordering_basp", "Ordering + BASP", udp_receive_single$name$X1)
udp_receive_single_plot <- ggplot(udp_receive_single, aes(x=name$X2, y=real_time / 1000, color=name$X1)) +
                           geom_line() +
                           geom_point(aes(shape=name$X1)) +
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
udp_receive_sequence$name$X1 <- gsub("BM_receive_udp_raw_sequence_inorder", "Inorder",  udp_receive_sequence$name$X1)
udp_receive_sequence$name$X1 <- gsub("BM_receive_udp_raw_sequence_dropped", "Dropped", udp_receive_sequence$name$X1)
udp_receive_sequence$name$X1 <- gsub("BM_receive_udp_raw_sequence_late",    "Late", udp_receive_sequence$name$X1)
udp_receive_sequence_plot <- ggplot(udp_receive_sequence, aes(x=name$X2, y=real_time / 1000, color=name$X1)) +
                                    geom_line() +
                                    geom_point(aes(shape=name$X1)) +
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
tcp$name$X1 <- as.character(tcp$name$X1)
tcp$name$X2 <- as.numeric(as.character(tcp$name$X2))
# Rename things.
tcp$name$X1 <- gsub("BM_send<raw_data_message, tcp_protocol<raw>>",         "RAW",  tcp$name$X1)
tcp$name$X1 <- gsub("BM_send<new_basp_message, tcp_protocol<stream_basp>>", "BASP", tcp$name$X1)
tcp_plot <- ggplot(tcp, aes(x=name$X2, y=real_time / 1000, color=name$X1)) +
                   geom_line() +
                   geom_point(aes(shape=name$X1)) +
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

basp <- split(tcp,tcp$name$X1)[['basp']]
raw <- split(tcp,tcp$name$X1)[['raw']]

diffs <- basp$real_time - raw$real_time

### pdf export
ggsave("figs/tcp_send.pdf", plot=tcp_plot, width=3.4, height=2.3)
### tikz export
tikz(file = "figs/tcp_send.tikz", sanitize=TRUE, width=3.4, height=2.3)
tcp_plot
dev.off()

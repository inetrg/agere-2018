library(tikzDevice)
library(ggplot2)
require(RColorBrewer)
require(gridExtra)

df <- read.csv("evaluation/layers.csv", sep=",", skip=8)
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

# drop everything > 16384
df <- df[! (df$size == 16384), ]

df$upper <- df$real_time + df$stddev
df$lower <- df$real_time - df$stddev

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
                 geom_line(size=0.8) +
                 geom_point(aes(shape=benchmark), stroke=1.3) +
                 geom_errorbar(
                   mapping=aes(
                     ymin=lower / 1000,
                     ymax=upper / 1000
                   ),
                   #size=2,
                   width=200
                 ) +
                 scale_shape_manual(values=c(1, 2, 4, 3)) +
                 theme_bw() +
                 theme(
                   legend.title=element_blank(),
                   legend.key=element_rect(fill='white'), 
                   legend.background=element_rect(fill="white", colour="black", size=0.25),
                   legend.direction="vertical",
                   legend.justification=c(0, 1),
                   legend.position=c(0, 1),
                   legend.box.margin=margin(c(3, 3, 3, 3)),
                   legend.key.size=unit(0.8, 'lines'),
                   text=element_text(size=9)
                 ) +
                 scale_color_brewer(type="qual", palette=6) +
                 labs(x="Payload Size [bytes]", y="Runtime [us]")
#ggsave("figs/udp_send.pdf", plot=udp_send_plot, width=3.4, height=2.3)
### tikz export
tikz(file="figs/udp_send.tikz", sanitize=TRUE, width=3.4, height=2.3)
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
udp_receive_single_plot <- ggplot(udp_receive_single, aes(x=size, y=real_time / 1000, color=benchmark)) +
                           geom_line(size=0.8) +
                           geom_point(aes(shape=benchmark), stroke=1.3) +
                           geom_errorbar(
                             mapping=aes(
                               ymin=lower / 1000,
                               ymax=upper / 1000
                             ),
                             #size=2,
                             width=200
                           ) +
                           scale_shape_manual(values=c(1, 2, 4, 3)) +
                           scale_y_continuous(limits=c(0, 0.35), breaks=seq(0, 0.3, 0.1)) + 
                           theme_bw() +
                           theme(
                             legend.title=element_blank(),
                             legend.key=element_rect(fill='white'), 
                             legend.background=element_rect(fill="white", colour="black", size=0.25),
                             legend.direction="horizontal",
                             legend.justification=c(0,1),
                             legend.position=c(0, 1),
                             #legend.margin=margin(0,0,0,0),
                             legend.box.margin=margin(c(3,3,3,3)),
                             legend.key.size=unit(0.8, 'lines'),
                             text=element_text(size=9)
                           ) +
                           scale_color_brewer(type="qual", palette=6) +
                           labs(x="Payload Size [bytes]", y="Runtime [us]")
#ggsave("figs/udp_receive_single.pdf", plot=udp_receive_single_plot, width=3.4, height=2.3)
### tikz export
tikz(file="figs/udp_receive_single.tikz", sanitize=TRUE, width=3.4, height=2.3)
udp_receive_single_plot
dev.off()

# Process sequence results.
udp_receive_sequence <- split(udp_receive,udp_receive$sequence)[['yes']]
udp_receive_sequence$benchmark <- gsub("BM_receive_udp_raw_sequence_inorder", "Inorder",  udp_receive_sequence$benchmark)
udp_receive_sequence$benchmark <- gsub("BM_receive_udp_raw_sequence_dropped", "Dropped", udp_receive_sequence$benchmark)
udp_receive_sequence$benchmark <- gsub("BM_receive_udp_raw_sequence_late",    "Late", udp_receive_sequence$benchmark)
udp_receive_sequence_plot <- ggplot(udp_receive_sequence, aes(x=size, y=real_time / 1000, color=benchmark)) +
                                    geom_line(size=0.8) +
                                    geom_point(aes(shape=benchmark), stroke=1.3) +
                                    geom_errorbar(
                                      mapping=aes(
                                        ymin=lower / 1000,
                                        ymax=upper / 1000
                                      ),
                                      #size=2,
                                      width=200
                                    ) +
                                    scale_shape_manual(values=c(4, 5, 6)) +
                                    scale_y_continuous(limits=c(0, 11.2), breaks=seq(0, 10, 2)) + 
                                    theme_bw() +
                                    theme(
                                      legend.title=element_blank(),
                                      legend.key=element_rect(fill='white'), 
                                      legend.background=element_rect(fill="white", colour="black", size=0.25),
                                      legend.direction="horizontal",
                                      legend.justification=c(0,1),
                                      legend.position=c(0,1),
                                      #legend.margin=margin(0,0,0,0),
                                      legend.box.margin=margin(c(3,3,3,3)),
                                      legend.key.size=unit(0.8, 'lines'),
                                      text=element_text(size=9)
                                    ) +
                                    scale_color_brewer(type="qual", palette=6) +
                                    labs(x="Payload Size [bytes]", y="Runtime [us]")
#ggsave("figs/udp_receive_sequence.pdf", plot=udp_receive_sequence_plot, width=3.4, height=2.3)
### tikz export
tikz(file="figs/udp_receive_sequence.tikz", sanitize=TRUE, width=3.4, height=2.3)
udp_receive_sequence_plot
dev.off()

# Get tcp related data.
tcp <- split(df,df$proto)[['tcp']]

# Split up by benchmark operation.
tcp$operation <- data.frame(ifelse(grepl("send", tcp$benchmark), "send", "receive"))
tcp_send <- split(tcp, tcp$operation)[['send']]
tcp_receive <- split(tcp, tcp$operation)[['receive']]

# Rename things.
tcp_send$benchmark <- gsub("BM_send<raw_data_message, tcp_protocol<raw>>",         "Raw",  tcp_send$benchmark)
tcp_send$benchmark <- gsub("BM_send<new_basp_message, tcp_protocol<stream_basp>>", "BASP", tcp_send$benchmark)

tcp_send_plot <- ggplot(tcp_send, aes(x=size, y=real_time/1000, color=benchmark)) +
                        geom_line(size=0.8) +
                        geom_point(aes(shape=benchmark), stroke=1.3) +
                        geom_errorbar(
                          mapping=aes(
                            ymin=lower/1000,
                            ymax=upper/1000
                          ),
                          #size=2,
                          width=200
                        ) +
                        scale_shape_manual(values=c(1, 3)) +
                        theme_bw() +
                        theme(
                          legend.title=element_blank(),
                          legend.key=element_rect(fill='white'), 
                          legend.background=element_rect(fill="white", colour="black", size=0.25),
                          legend.direction="vertical",
                          legend.justification=c(0, 1),
                          legend.position=c(0, 1),
                          legend.box.margin=margin(c(3, 3, 3, 3)),
                          legend.key.size=unit(0.8, 'lines'),
                          text=element_text(size=9)
                        ) +
                        scale_color_brewer(type="qual", palette=6) +
                        #scale_colour_manual(values=brewer.pal(n=4, name="Dark2")[-c(2,3)]) + # choose colors to match the other plots
                        labs(x="Payload Size [bytes]", y="Runtime [us]")

# colors <- brewer.pal(n=7, "Oranges")[3:9]
#basp <- split(tcp,tcp$benchmark)[['BASP']]
#raw <- split(tcp,tcp$benchmark)[['Raw']]
#diffs <- basp$real_time - raw$real_time

### pdf export
#ggsave("figs/tcp_send.pdf", plot=tcp_plot, width=3.4, height=2.3)
### tikz export
tikz(file="figs/tcp_send.tikz", sanitize=TRUE, width=3.4, height=2.3)
tcp_send_plot
dev.off()

tcp_receive$benchmark <- gsub("BM_receive_tcp_raw",  "Raw",  tcp_receive$benchmark)
tcp_receive$benchmark <- gsub("BM_receive_tcp_basp", "BASP", tcp_receive$benchmark)

tcp_receive_plot <- ggplot(tcp_receive, aes(x=size, y=real_time/1000, color=benchmark)) +
                           geom_line(size=0.8) +
                           geom_point(aes(shape=benchmark), stroke=1.3) +
                           geom_errorbar(
                             mapping=aes(
                               ymin=lower/1000,
                               ymax=upper/1000
                             ),
                             #size=2,
                             width=200
                           ) +
                           scale_shape_manual(values=c(1, 3)) +
                           #scale_shape_manual(values=c(0, 1, 2, 3)) +
                           scale_y_continuous(limits=c(0, 0.35), breaks=seq(0, 0.3, 0.1)) + 
                           theme_bw() +
                           theme(
                             legend.title=element_blank(),
                             legend.key=element_rect(fill='white'), 
                             legend.background=element_rect(fill="white", colour="black", size=0.25),
                             legend.direction="horizontal",
                             legend.justification=c(0, 1),
                             legend.position=c(0, 1),
                             legend.box.margin=margin(c(3, 3, 3, 3)),
                             legend.key.size=unit(0.8, 'lines'),
                             text=element_text(size=9)
                           ) +
                           scale_color_brewer(type="qual", palette=6) +
                           #scale_colour_manual(values=brewer.pal(n=4, name="Set1")[-c(2,3)]) + # choose colors to match the other plots
                           labs(x="Payload Size [bytes]", y="Runtime [us]")

tikz(file="figs/tcp_receive.tikz", sanitize=TRUE, width=3.4, height=2.3)
tcp_receive_plot
dev.off()


#tikz(file="figs/send_combined.tikz", sanitize=TRUE, width=3.4, height=2.3)
#grid.arrange(tcp_send_plot, udp_send_plot, ncol=2)
#dev.off()

# Create a plot that combines sending data.
tcp_send_cleaned <- tcp_send[c("benchmark", "size", "real_time", "upper", "lower")]
udp_send_cleaned <- udp_send[c("benchmark", "size", "real_time", "upper", "lower")]
tcp_send_cleaned$proto <- 'TCP'
udp_send_cleaned$proto <- 'UDP'

send_combined <- rbind(tcp_send_cleaned, udp_send_cleaned)

combined_send_plot <- ggplot(send_combined, aes(x=size, y=real_time/1000, color=benchmark)) +
                             geom_line(size=0.8) +
                             geom_point(aes(shape=benchmark), stroke=1.3) +
                             geom_errorbar(
                               mapping=aes(
                                 ymin=lower/1000,
                                 ymax=upper/1000
                               ),
                               #size=2,
                               width=400
                             ) +
                             scale_shape_manual(values=c(1, 2, 4, 3)) +
                             facet_grid(cols=vars(proto)) +
                             #facet_wrap() +
                             theme_bw() +
                             theme(
                               legend.title=element_blank(),
                               legend.key=element_rect(fill='white'), 
                               legend.background=element_rect(fill="white", colour="black", size=0.25),
                               legend.direction="vertical",
                               legend.justification=c(0, 1),
                               legend.position=c(0, 1),
                               legend.box.margin=margin(c(3, 3, 3, 3)),
                               legend.key.size=unit(0.8, 'lines'),
                               text=element_text(size=9),
                               strip.background=element_blank(),
                               strip.text.x=element_blank()
                             ) +
                             scale_color_brewer(type="qual", palette=6) +
                             #scale_color_grey() +
                             labs(x="Payload Size [bytes]", y="Runtime [us]")

tikz(file="figs/send_combined.tikz", sanitize=TRUE, width=3.4, height=2.3)
combined_send_plot
dev.off()


# Create a plot that combines receiving data.
tcp_receive_cleaned <- tcp_receive[c("benchmark", "size", "real_time", "upper", "lower")]
udp_receive_cleaned <- udp_receive_single[c("benchmark", "size", "real_time", "upper", "lower")]
tcp_receive_cleaned$proto <- 'TCP'
udp_receive_cleaned$proto <- 'UDP'

receive_combined <- rbind(tcp_receive_cleaned, udp_receive_cleaned)

combined_receive_plot <- ggplot(receive_combined, aes(x=size, y=real_time/1000, color=benchmark)) +
                                geom_line(size=0.8) +
                                geom_point(aes(shape=benchmark), stroke=1.3) +
                                geom_errorbar(
                                  mapping=aes(
                                    ymin=lower/1000,
                                    ymax=upper/1000
                                  ),
                                  #size=2,
                                  width=400
                                ) +
                                scale_shape_manual(values=c(1, 2, 4, 3)) +
                                facet_grid(cols=vars(proto)) +
                                #facet_wrap() +
                                theme_bw() +
                                theme(
                                  legend.title=element_blank(),
                                  legend.key=element_rect(fill='white'), 
                                  legend.background=element_rect(fill="white", colour="black", size=0.25),
                                  legend.direction="vertical",
                                  legend.justification=c(0, 1),
                                  legend.position=c(0, 0.8),
                                  legend.box.margin=margin(c(3, 3, 3, 3)),
                                  legend.key.size=unit(0.8, 'lines'),
                                  text=element_text(size=9),
                                  strip.background=element_blank(),
                                  strip.text.x=element_blank()
                                ) +
                                scale_color_brewer(type="qual", palette=6) +
                                labs(x="Payload Size [bytes]", y="Runtime [us]")
  
tikz(file="figs/receive_combined.tikz", sanitize=TRUE, width=3.4, height=2.3)
combined_receive_plot
dev.off()


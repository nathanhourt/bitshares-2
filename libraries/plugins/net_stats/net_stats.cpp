/*
 * Copyright (c) 2018 net_stats and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <graphene/net_stats/net_stats.hpp>

namespace graphene { namespace net_stats {

namespace detail {

class net_stats_impl {
public:
    net_stats_impl(net_stats_plugin& _plugin)
        : _self(_plugin) {}
    ~net_stats_impl() = default;

    void onBlock(const signed_block& b);

    graphene::chain::database& database()
    {
        return _self.database();
    }

    net_stats_plugin& _self;

    std::string _plugin_option = "";

private:
};

void net_stats_impl::onBlock(const signed_block& b) {
    wdump((b.block_num()));
}

} // end namespace detail

net_stats_plugin::net_stats_plugin() :
    my(new detail::net_stats_impl(*this)) {
}

net_stats_plugin::~net_stats_plugin() = default;

std::string net_stats_plugin::plugin_name()const {
    return "net_stats";
}

std::string net_stats_plugin::plugin_description()const {
    return "net_stats description";
}

void net_stats_plugin::plugin_set_program_options(boost::program_options::options_description& cli,
                                                  boost::program_options::options_description& cfg) {
    cli.add_options()
            ("net_stats_option", boost::program_options::value<std::string>(), "net_stats option")
            ;
    cfg.add(cli);
}

void net_stats_plugin::plugin_initialize(const boost::program_options::variables_map& options) {
    database().applied_block.connect([&](const signed_block& b) {
        my->onBlock(b);
    });

    if (options.count("net_stats")) {
        my->_plugin_option = options["net_stats"].as<std::string>();
    }
}

void net_stats_plugin::plugin_startup() {
    ilog("net_stats: plugin_startup() begin");
}

} }

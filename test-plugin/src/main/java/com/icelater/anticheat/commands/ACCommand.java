package com.icelater.anticheat.commands;

import com.icelater.anticheat.AdvancedAntiCheat;
import org.bukkit.command.Command;
import org.bukkit.command.CommandExecutor;
import org.bukkit.command.CommandSender;

public class ACCommand implements CommandExecutor {
    @Override
    public boolean onCommand(CommandSender sender, Command cmd, String label, String[] args) {
        if (args.length == 0) {
            sender.sendMessage("§aAdvancedAntiCheat commands: reload, status");
            return true;
        }
        switch (args[0].toLowerCase()) {
            case "reload":
                if (sender.hasPermission("anticheat.reload")) {
                    AdvancedAntiCheat.getInstance().reloadConfig();
                    sender.sendMessage("§aConfig reloaded!");
                } else {
                    sender.sendMessage("§cNo permission!");
                }
                break;
            case "status":
                if (sender.hasPermission("anticheat.status")) {
                    sender.sendMessage("§aActive checks: " + AdvancedAntiCheat.getInstance().getCheckManager().getChecks().keySet());
                } else {
                    sender.sendMessage("§cNo permission!");
                }
                break;
            default:
                sender.sendMessage("§cUnknown command.");
        }
        return true;
    }
                      }

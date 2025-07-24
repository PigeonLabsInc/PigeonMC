package com.icelater.anticheat.checks;

import java.util.Map;

public class CheckConfig {
    private final Map<String, Object> settings;

    public CheckConfig(Map<String, Object> settings) {
        this.settings = settings;
    }

    public <T> T get(String key, Class<T> type) {
        return type.cast(settings.get(key));
    }
}

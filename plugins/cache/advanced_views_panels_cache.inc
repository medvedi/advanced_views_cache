<?php

/**
 * @file
 * Provides views spicific caching for panels
 */

// Plugin definition
$plugin = array(
  'title' => t("Advanced views cache"),
  'description' => t("Advanced cache to handle views without instantiating the view object"),
  'cache get' => 'advanced_views_panels_cache_get_cache',
  'cache set' => 'advanced_views_panels_cache_set_cache',
  'cache clear' => 'advanced_views_panels_cache_clear_cache',
  'settings form' => 'advanced_views_panels_cache_settings_form',
  'settings form submit' => 'advanced_views_panels_cache_settings_form_submit',
  'defaults' => array(
    'per_user_cache' => TRUE,
  ),
);

/**
 * Get cached content.
 */
function advanced_views_panels_cache_get_cache($conf, $display, $args, $contexts, $pane) {
  // We call it first to ensure context is prepared for Ajax pager even for anonymous users.
  $cid = _advanced_views_panels_cache_get_cid($conf, $display, $args, $contexts, $pane);

  // Do not cache for anonymous users - Varnish/anonymous cache handler will handle that.
  if (user_is_anonymous()) {
    return FALSE;
  }

  list($view_name, $view_display) = _advanced_views_panels_cache_get_view_info($pane);

  if ($cid) {
    $cache = cache_get($cid, 'cache_views_ajax');
    if (!$cache) {
      return FALSE;
    }
    // always autoexpire after 30 mins
    if ((time() - $cache->created) > 1800) {
      return FALSE;
    }

    // Allow other module react/alter cache before it is returned.
    foreach (module_implements('advanced_views_cache_get') as $module) {
      $function = $module . '_advanced_views_cache_get';
      $function($view_name, $view_display, $cache);
    }

    return $cache->data['output'];
  }

  return FALSE;
}

/**
 * Set cached content.
 */
function advanced_views_panels_cache_set_cache($conf, $content, $display, $args, $contexts, $pane) {
  $cid = _advanced_views_panels_cache_get_cid($conf, $display, $args, $contexts, $pane);
  $view_name = _advanced_views_panels_cache_get_view_info($pane);
  list($view_name, $view_display) = $view_name;

  // Do not cache for anonymous users - Varnish/anonymous cache handler
  // will handle that. The main reason for that is flagging allowed for most of
  // the views which will not work if anonymous version of the page was cached.
  // @todo: this may be too restrictive, should it be moved to a hook?
  if (user_is_anonymous() || !$cid) {
    return;
  }

  // We save both view output and its result.
  $result = drupal_static('advanced_views_cache_last_result', array());
  $cache = array('output' => $content, 'result' => $result);

  // Allow other module react/alter cache before it is saved.
  foreach (module_implements('advanced_views_cache_set') as $module) {
    $function = $module . '_advanced_views_cache_set';
    $function($view_name, $view_display, $cache);
  }

  // Consider cache lifetime.
  cache_set($cid, $cache, 'cache_views_ajax', advanced_views_cache_get_view_expire($view_name, $view_display));
  drupal_static_reset('advanced_views_cache_last_result');
}

/**
 * Clear cached content.
 *
 * Cache clears are always for an entire display, regardless of arguments.
 */
function advanced_views_panels_cache_clear_cache($display) {
//  $cid = _advanced_views_panels_cache_get_cid($display, $display, $args, $contexts, $pane);
//  cache_clear_all($cid, 'cache_views_ajax', TRUE);
}

/**
 * Figure out an id for our cache based upon input and settings.
 */
function _advanced_views_panels_cache_get_cid($conf, $display, $args, $contexts, $pane) {
  global $language;
  $cache_context = &drupal_static('advanced_views_cache_context', 0);
  $cid = NULL;

  // Certain view may have different contents based on contextual filters, and
  // it is always a different page that provides a different context, so we use
  // current path as a part of cache key.
  if ($pane->type == 'views_panes') {
    $view_display = str_replace('-', ':', $pane->subtype);
  }
  elseif ($pane->type == 'views') {
    $view_display = $pane->subtype . ':' . $pane->configuration['display'];
  }
  // Unknown pane type/subtype.
  else {
    return NULL;
  }

  $cache_prefix = '';
  // Check if we can use current_path() in case when user-specific view is rendered.
  if ($conf['per_user_cache']) {
    foreach ($contexts as $context) {
      // @todo: that kind of comparison is probably not ideal.
      if ($context->type == array('entity:user', 'entity', 'user')) {
        if (is_object($context->data) && isset($context->data->uid)) {
          $cache_prefix = 'user/' . $context->data->uid . ':';
          $cache_context = $context->data->uid;
          break;
        }
      }
    }
  }

  $cache_suffix = '';
  $view = advanced_views_cache_get_cached_views($view_display);
  if (!empty($view['path_context'])) {
    $cache_suffix = ':' . current_path();
  }

  $cid = $cache_prefix . $view_display . ':' . $language->language . $cache_suffix . ':pane';

  // CUSTOM CODE BEGIN
  // Blog list query parametr.
  foreach ($_REQUEST as $key => $value) {
    if (strpos($key,'arkiv') !== false) {
      $cid .= ':' . $key;
    }
  }
  // CUSTOM CODE END

  return $cid;
}

/**
 * Get view details out of pane configuration.
 */
function _advanced_views_panels_cache_get_view_info($pane) {
  $view_info = array();
  if ($pane->type == 'views_panes' && !empty($pane->subtype)) {
    $view_info = explode('-', $pane->subtype);
  }
  elseif ($pane->type == 'views') {
    $view_name = $pane->subtype;
    $view_display = $pane->configuration['display'];
    $view_info = array($view_name, $view_display);
  }
  return $view_info;
}

/**
 * Various settings.
 */
function advanced_views_panels_cache_settings_form($conf, $display, $pid) {
  $user_contexts = array();
  $form = array();
  if (!empty($display->context)) {
    foreach ($display->context as $context) {
      if ($context->type == array('entity:user', 'entity', 'user')) {
        $user_contexts[] = $context->identifier . ' (' . $context->id . ')';
      }
    }
  }

  if (!empty($user_contexts)) {
    // @todo: we may need to check for multiple contexts and allow choosing the
    // one that stores user ID.
    $form['per_user_cache'] = array(
      '#title' => t('Per-user cache'),
      '#description' => t('The following supported context were detected: @contexts. If checked we will use it to ensure per-user caching.', array('@contexts' => join(', ', $user_contexts))),
      '#type' => 'checkbox',
      '#default_value' => $conf['per_user_cache'],
    );
  }

  return $form;
}

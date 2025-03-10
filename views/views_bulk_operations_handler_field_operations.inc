<?php

/**
* @file
* Views field handler. Contains all relevant VBO options and related logic.
* Implements the Views Form API.
*/

class ViewsBulkOperationsHandlerFieldOperations extends views_handler_field_entity {
  var $revision = FALSE;

  function init(&$view, &$options) {
    parent::init($view, $options);

    // Update old settings. This may not be necessary in Backdrop as we are
    // starting from scratch 
    // @todo when importing Views from D7 that used VBO, all settings have to be
    // renamed.
    if (!empty($options['vbo']) && empty($this->options['vbo_operations'])) {
      $this->options['vbo_operations'] = $options['vbo']['operations'];
      unset($options['vbo']['operations']);
      $this->options['vbo_settings'] = $options['vbo'] + $this->options['vbo_settings'];
    }
    // When updating old Views it is possible for this value to stay empty.
    if (empty($this->options['vbo_settings']['entity_load_capacity'])) {
      $this->options['vbo_settings']['entity_load_capacity'] = 10;
    }

    foreach ($this->options['vbo_operations'] as $operation_id => &$operation_options) {
      // Prefix all un-prefixed operations.
      if (strpos($operation_id, '::') === FALSE) {
        $operations = views_bulk_operations_get_operation_info();
        // Basically, guess.
        foreach (array('action', 'rules_component') as $operation_type) {
          $new_operation_id = $operation_type . '::' . $operation_id;
          if (isset($operations[$new_operation_id])) {
            $this->options['vbo_operations'][$new_operation_id] = $operation_options;
            break;
          }
        }

        // Remove the old operation in any case.
        unset($this->options['vbo_operations'][$operation_id]);
      }

      // Rename the use_queue setting.
      if (isset($operation_options['use_queue']) && !isset($operation_options['postpone_processing'])) {
        $operation_options['postpone_processing'] = $operation_options['use_queue'];
        unset($operation_options['use_queue']);
      }
    }

    // Check whether this is a revision.
    $table_data = views_fetch_data($this->table);
    if (!empty($table_data['table']['revision'])) {
      $this->revision = TRUE;
    }
  }

  function option_definition() {
    $options = parent::option_definition();

    $options['vbo_settings'] = array(
      'contains' => array(
        'display_type' => array('default' => 0),
        'enable_select_all_pages' => array('default' => TRUE),
        'row_clickable' => array('default' => TRUE),
        'force_single' => array('default' => FALSE),
        'entity_load_capacity' => array('default' => 10),
        'skip_batching' => array('default' => 0),
        'save_view_object_when_batching' => array('default' => 0),
      ),
    );
    $options['vbo_operations'] = array(
      'default' => array(),
      'unpack_translatable' => 'unpack_operations',
      //'export' => 'export_vbo_operations',
    );

    return $options;
  }

  function unpack_operations(&$translatable, $storage, $option, $definition, $parents, $keys) {
    $translatable[] = array(
      'value' => t('- Choose an operation -'),
      'keys' => array_merge($keys, array('noop')),
    );
    foreach ($storage[$option] as $key => $operation) {
      if (!empty($operation['override_label']) && !empty($operation['label'])) {
        $translatable[] = array(
          'value' => $operation['label'],
          'keys' => array_merge($keys, array($key)),
        );
      }
    }
  }

  function options_form(&$form, &$form_state) {
    parent::options_form($form, $form_state);

    $form['vbo_settings'] = array(
      '#type' => 'fieldset',
      '#title' => t('Views bulk operations settings'),
      '#collapsible' => TRUE,
      '#collapsed' => TRUE,
    );
    $form['vbo_settings']['display_type'] = array(
      '#type' => 'radios',
      '#title' => t('Display operations as'),
      '#default_value' => $this->options['vbo_settings']['display_type'],
      '#options' => array(
        t('Dropdown selectbox with Submit button'),
        t('Each action as a separate button'),
      ),
    );
    $form['vbo_settings']['enable_select_all_pages'] = array(
      '#type' => 'checkbox',
      '#title' => t('Enable "Select all items on all pages"'),
      '#default_value' => $this->options['vbo_settings']['enable_select_all_pages'],
      '#description' => t('Check this box to enable the ability to select all items on all pages.'),
    );
    $form['vbo_settings']['row_clickable'] = array(
      '#type' => 'checkbox',
      '#title' => t('Make the whole row clickable'),
      '#default_value' => $this->options['vbo_settings']['row_clickable'],
      '#description' => t('Check this box to select an item when its row has been clicked. Requires JavaScript.'),
    );
    $form['vbo_settings']['force_single'] = array(
      '#type' => 'checkbox',
      '#title' => t('Force single'),
      '#default_value' => $this->options['vbo_settings']['force_single'],
      '#description' => t('Check this box to restrict selection to a single value.'),
    );
    $form['vbo_settings']['entity_load_capacity'] = array(
      '#type' => 'textfield',
      '#title' => t('Number of entities to load at once'),
      '#description' => t("Improve execution performance at the cost of memory usage. Set to '1' if you're having problems."),
      '#default_value' => $this->options['vbo_settings']['entity_load_capacity'],
    );
    $form['vbo_settings']['skip_batching'] = array(
      '#type' => 'checkbox',
      '#title' => t('Skip batching'),
      '#default_value' => $this->options['vbo_settings']['skip_batching'],
      '#description' => '<b>' . t('Warning:') . '</b> ' . t('This will cause timeouts for larger amounts of selected items.'),
    );
    $form['vbo_settings']['save_view_object_when_batching'] = array(
      '#type' => 'checkbox',
      '#title' => t('Save the whole view object when batching'),
      '#default_value' => $this->options['vbo_settings']['save_view_object_when_batching'],
      '#description' => '<b>' . t('Warning:') . '</b> ' . t('Use this option when your view contains query conditions which are not defined as arguments.'),
    );

    // Display operations and their settings.
    $form['vbo_operations'] = array(
      '#tree' => TRUE,
      '#type' => 'fieldset',
      '#title' => t('Selected bulk operations'),
      '#collapsible' => TRUE,
      '#collapsed' => FALSE,
    );

    $entity_type = $this->get_entity_type();
    $options = $this->options['vbo_operations'];
    foreach (views_bulk_operations_get_applicable_operations($entity_type, $options) as $operation_id => $operation) {
      $operation_options = !empty($options[$operation_id]) ? $options[$operation_id] : array();
      
      // Used to hide options that do not apply to non-VBO actions. 
      $isVboAction = $operation->isVboAction();

      $dom_id = 'edit-options-vbo-operations-' . str_replace(array('_', ':'), array('-', ''), $operation_id);
      $form['vbo_operations'][$operation_id]['selected'] = array(
        '#type' => 'checkbox',
        '#title' => $operation->adminLabel(),
        '#default_value' => !empty($operation_options['selected']),
      );
      if (!$operation->aggregate()) {
        $form['vbo_operations'][$operation_id]['postpone_processing'] = array(
          '#type' => 'checkbox',
          '#title' => t('Enqueue the operation instead of executing it directly'),
          '#default_value' => !empty($operation_options['postpone_processing']),
          '#access' => $isVboAction,
          '#dependency' => array(
            $dom_id . '-selected' => array(1),
          ),
        );
      }
      $form['vbo_operations'][$operation_id]['skip_confirmation'] = array(
        '#type' => 'checkbox',
        '#title' => t('Skip confirmation step'),
        '#default_value' => !empty($operation_options['skip_confirmation']) || !$isVboAction, // Skip confirmation for core actions, as they define their own confirmations.
        '#access' => $isVboAction,
        '#dependency' => array(
          $dom_id . '-selected' => array(1),
        ),
      );
      $form['vbo_operations'][$operation_id]['skip_permission_check'] = array(
        '#type' => 'checkbox',
        '#title' => t('Skip permission step'),
        '#default_value' => !empty($operation_options['skip_permission_check']),
        '#access' => $isVboAction,
        '#dependency' => array(
          $dom_id . '-selected' => array(1),
        ),
      );

      $form['vbo_operations'][$operation_id] += $operation->adminOptionsForm($dom_id, $this);
    }
  }

  function options_validate(&$form, &$form_state) {
    parent::options_validate($form, $form_state);

    $entity_type = $this->get_entity_type();
    foreach ($form_state['values']['options']['vbo_operations'] as $operation_id => &$options) {
      if (empty($options['selected'])) {
        continue;
      }

      $operation = views_bulk_operations_get_operation($operation_id, $entity_type, $options);
      $fake_form = $form['vbo_operations'][$operation_id];
      $fake_form_state = array('values' => &$options);
      $error_element_base = 'vbo_operations][' . $operation_id . '][';
      $operation->adminOptionsFormValidate($fake_form, $fake_form_state, $error_element_base);
    }
  }

  function options_submit(&$form, &$form_state) {
    parent::options_submit($form, $form_state);

    $entity_type = $this->get_entity_type();
    foreach ($form_state['values']['options']['vbo_operations'] as $operation_id => &$options) {
      if (empty($options['selected'])) {
        continue;
      }

      $operation = views_bulk_operations_get_operation($operation_id, $entity_type, $options);
      $fake_form = $form['vbo_operations'][$operation_id];
      $fake_form_state = array('values' => &$options);
      $operation->adminOptionsFormSubmit($fake_form, $fake_form_state);
    }
  }

  /**
   * Returns the value of a vbo option.
   */
  function get_vbo_option($key, $default = NULL) {
    return isset($this->options['vbo_settings'][$key]) ? $this->options['vbo_settings'][$key] : $default;
  }

  /**
   * If the view is using a table style, provide a
   * placeholder for a "select all" checkbox.
   */
  function label() {
    if (!empty($this->view->style_plugin) && $this->view->style_plugin instanceof views_plugin_style_table && !$this->options['vbo_settings']['force_single']) {
      return '<!--views-bulk-operations-select-all-->';
    }
    else {
      return parent::label();
    }
  }

  function render($values) {
    return '<!--form-item-' . $this->options['id'] . '--' . $this->view->row_index . '-->';
  }

  /**
   * The form which replaces the placeholder from render().
   */
  function views_form(&$form, &$form_state) {
    // The view is empty, abort.
    if (empty($this->view->result)) {
      return;
    }

    $form[$this->options['id']] = array(
      '#tree' => TRUE,
    );
    // At this point, the query has already been run, so we can access the results
    // in order to get the base key value (for example, nid for nodes).
    foreach ($this->view->result as $row_index => $row) {
      $this->view->row_index = $row_index;
      $id = $this->get_value($row, $this->real_field);

      if ($this->options['vbo_settings']['force_single']) {
        $form[$this->options['id']][$row_index] = array(
          '#type' => 'radio',
          '#parents' => array($this->options['id']),
          '#return_value' => $id,
          '#attributes' => array('class' => array('vbo-select')),
        );
      }
      else {
        $form[$this->options['id']][$row_index] = array(
          '#type' => 'checkbox',
          '#return_value' => $id,
          '#default_value' => FALSE,
          '#attributes' => array('class' => array('vbo-select')),
        );
      }
    }
  }

  public function get_selected_operations() {
    global $user;
    $selected = backdrop_static(__FUNCTION__);
    if (!isset($selected)) {
      $entity_type = $this->get_entity_type();
      $selected = array();
      foreach ($this->options['vbo_operations'] as $operation_id => $options) {
        if (empty($options['selected'])) {
          continue;
        }
        $operation = views_bulk_operations_get_operation($operation_id, $entity_type, $options);
        if (!$operation) {
          continue;
        }
        $skip_permission_check = $operation->getAdminOption('skip_permission_check', FALSE);
        if (!$operation->access($user) && !$skip_permission_check) {
          continue;
        }
        $selected[$operation_id] = $operation;
      }
    }

    return $selected;
  }

  /**
   * Returns the options stored for the provided operation id.
   */
  public function get_operation_options($operation_id) {
    $options = $this->options['vbo_operations'];
    return isset($options[$operation_id]) ? $options[$operation_id] : array();
  }

  /**
   * Determine the base table of the VBO field, and then use it to determine
   * the entity type that VBO is operating on.
   */
  public function get_entity_type() {
    return $this->entity_type;
  }

}

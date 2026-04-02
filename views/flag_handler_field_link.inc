<?php
/**
 * @file
 * Definition of views_handler_field_node_link.
 */

/**
 * Field handler to present a link to the node.
 *
 * @ingroup views_field_handlers
 */
class flag_handler_field_link extends views_handler_field_entity {

  /**
   * Returns the flag object associated with our field.
   *
   * A field is in some relationship. This function reaches out for this
   * relationship and reads its 'flag' option, which holds the flag name.
   */
  function get_flag() {
    // When editing a view it's possible to delete the relationship (either by
    // error or to later recreate it), so we have to guard against a missing
    // one.
    if (isset($this->view->relationship[$this->options['relationship']])) {
      return $this->view->relationship[$this->options['relationship']]->get_flag();
    }
  }

  /**
   * Return the the relationship we're linked to. That is, the alias for its
   * table (which is suitbale for use with the various methods of the 'query'
   * object).
   */
  function get_parent_relationship() {
    $parent = $this->view->relationship[$this->options['relationship']]->options['relationship'];
    if (!$parent || $parent == 'none') {
      // Base query table.
      return NULL;
    } else {
      return $this->view->relationship[$parent]->alias;
    }
  }

  function option_definition() {
    $options = parent::option_definition();
    $options['url'] = array('default' => FALSE, 'bool' => TRUE);
    $options['absolute'] = array('default' => FALSE, 'bool' => TRUE);
    $options['text'] = array('default' => '', 'translatable' => TRUE);
    return $options;
  }

  function options_form(&$form, &$form_state) {
    $form['url'] = array(
      '#type' => 'checkbox',
      '#title' => t('Output URL as text'),
      '#default_value' => $this->options['url'],
      '#description' => t('The resulting URL will be plain text and not a clickable link.'),
    );
    $form['text'] = array(
      '#type' => 'textfield',
      '#title' => t('Text to display'),
      '#default_value' => $this->options['text'],
      '#indentation' => 1,
      '#states' => array(
        'visible' => array(
          ':input[name="options[url]"]' => array('checked' => FALSE),
        ),
      ),
    );
    $form['absolute'] = array(
      '#type' => 'checkbox',
      '#title' => t('Output an absolute link starting with "http(s)://"'),
      '#default_value' => $this->options['absolute'],
    );
    parent::options_form($form, $form_state);

    // The path is set by render_link function so don't allow to set it.
    $form['alter']['path'] = array('#access' => FALSE);
    $form['alter']['external'] = array('#access' => FALSE);
    $form['alter']['absolute'] = array('#access' => FALSE);
  }

  /**
   * Override base ::query(). The purpose here is to make it possible for the
   * render() method to know two things: what's the content ID, and whether
   * it's flagged.
   */
  function query() {
    if (!($flag = $this->get_flag())) {
      // Error message is printed in render().
      return;
    }
    $info = $flag->get_views_info();
    $parent = $this->get_parent_relationship();

    // Find out if the content is flagged. We can't just peek at some field in
    // our loaded table because it doesn't always reflect the user browsing the
    // page. So we explicitly add the flagging table to find this out.
    // If the relationship is created with 'Current User' checked, we probably
    // already have the appropriate join. We just need the appropriate table
    // alias for that join. We look in the relationship settings to see if
    // user_scope is set to 'current' to determine this.
    $relationship = $this->view->relationship[$this->options['relationship']];
    if (isset($relationship->options['user_scope']) && $relationship->options['user_scope'] == 'current') {
      $table_alias = $relationship->alias;
    }
    // Otherwise, let's set up the alias, keeping it unique for this flag in
    // case there are multiple flag relationships in a single view.
    else {
      $table_alias = 'flagging_current_user_' . $flag->name;
    }
    // Now that we have the table alias, let's see if it's already been joined
    // to. If it hasn't, we'll set up a join.
    if (!isset($this->query->table_queue[$table_alias])) {
      $join = new views_join();
      $join->construct('flagging', $info['views table'], $info['join field'], 'entity_id');
      $join->extra[] = array(
        'field' => 'flag_name',
        'value' => $flag->name,
        'numeric' => FALSE,
      );
      if (!$flag->global) {
        $join->extra[] = array(
          'field' => 'uid',
          'value' => '***CURRENT_USER***',
          'numeric' => TRUE,
        );
        $join->extra[] = array(
          'field' => 'sid',
          'value' => '***FLAG_CURRENT_USER_SID***',
          'numeric' => TRUE,
        );
      }
      $table_alias = $this->query->add_table($table_alias, $parent, $join);
    }
    $this->aliases['is_flagged'] = $this->query->add_field($table_alias, 'entity_id');

    // Next, find out the content ID. We can't add_field() on this table
    // (flagging), because its entity_id may be NULL (in case no user has
    // flagged this content, and it's a LEFT JOIN). So we reach to the parent
    // relationship and add_field() *its* content ID column.
    $left_table = $this->view->relationship[$this->options['relationship']]->table_alias;
    $this->aliases['entity_id'] = $this->query->add_field($left_table, $info['join field']);
  }

  /**
   * Find out if the flag applies to each item seen on the page. It's done in a
   * separate DB query to to avoid complexity and to make 'many to one' tests
   * (e.g. checking user roles) possible without causing duplicate rows.
   */
  function pre_render(&$values) {
    if (!($flag = $this->get_flag())) {
      // Error message is printed in render().
      return;
    }

    $ids = array();
    foreach ($values as $row) {
      $entity_id = $row->{$this->aliases['entity_id']};
      $is_flagged = $row->{$this->aliases['is_flagged']};
      if (isset($entity_id)) {
        $ids[$entity_id] = $is_flagged ? 'unflag' : 'flag';
      }
    }
    $this->flag_applies = $ids ? $flag->access_multiple($ids) : array();
  }

  function render($values) {
    if (!($flag = $this->get_flag())) {
      // get_flag() itself will print a more detailed message.
      return t('Missing flag');
    }

    return $this->render_link($flag, $values);
  }

  function render_link($flag, $values) {
    $entity_id = $values->{$this->aliases['entity_id']};
    $is_flagged = $values->{$this->aliases['is_flagged']};

    if (empty($this->flag_applies[$entity_id])) {
      // Flag does not apply to this content.
      return;
    }

    $options = &$this->options;

    if ($options['url']) {
      return url("node/$entity_id", array('absolute' => $options['absolute']));
    }
    else {
      $options['alter']['make_link'] = TRUE;
      $options['alter']['absolute'] = $options['absolute'];
      $options['alter']['path'] = "node/$entity_id";
      $text = !empty($options['text']) ? $options['text'] : t('view');
      return $text;
    }
  }
}

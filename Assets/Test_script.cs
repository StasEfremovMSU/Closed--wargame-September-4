using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class Test_script : MonoBehaviour {

    public GameObject obj;
    public GameObject cub;
    private Light my_light;
    public int t1 = 0;

    //[SerializeField]
    private int t2 = 0;

    // Use this for initialization
    void Start()
    {

    }

    // При отрисовки кадра
    void Update()
    {


        if (Input.GetKeyUp(KeyCode.Space))
        {
            my_light = obj.GetComponent<Light>();
            my_light.enabled = !my_light.enabled;
        }

        if (Input.GetKeyUp(KeyCode.K))
        {
            /// Убрать объект со цены
            cub.SetActive(false);
            cub.SetActive(true);

            /// Удалить объект из сцены
            Destroy(cub);
        }
        if (Input.GetKeyUp(KeyCode.R))
        {
            Debug.Log("FixedUpdate time : " + Time.deltaTime);
            cub.GetComponent<Renderer>().material.color = Color.red;
        }
    }

    void FixedUpdate() /// срабатывает через 0.03
    {

    }
}
